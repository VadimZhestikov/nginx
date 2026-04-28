#!/usr/bin/perl

# Tests for graceful shutdown (SIGQUIT) when a wholeBodyAsync body-filter
# request is suspended waiting for a Promise that never settles
# (KnownFailure_2: Step 7).
#
# Before this fix:
#   - Only bf_p->promise was freed in exit_process; bf_p->gen was leaked,
#     causing the QuickJS "list_empty(&rt->gc_obj_list)" assertion.
#   - r->main->count > 0 was never decremented, leaving an open connection
#     fd that triggered an "[alert] open socket ... left in connection" in
#     ngx_worker_process_exit().
#
# After this fix:
#   - Both promise and gen JSValues are freed.
#   - ngx_http_finalize_request(r, NGX_ERROR) is called to release count
#     and close the connection, preventing the open-socket alert.
#   - Same treatment for sf_pending (streaming async) and l4_pending (L4).

use warnings;
use strict;
use Test::More;
use POSIX qw(WNOHANG);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 1;

js_source %%TESTDIR%%/bf_drain_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /echo/  { }
        location /hang/  { }
    }
}
EOF

$t->write_file('bf_drain_init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /echo/ — sanity check: normal round-trip. */
    by['/echo/'].handler = function(r) {
        r.respond(200, {'content-type': 'text/plain'}, 'ok');
    };

    /* /hang/ — wholeBodyAsync filter returns a Promise that never settles.
     * This leaves an entry in w->bf_pending that hangs until the worker exits. */
    by['/hang/'].addBodyFilter('wholeBodyAsync', function(r, body) {
        return new Promise(function() { /* intentionally never resolve */ });
    });
    by['/hang/'].handler = function(r) {
        r.respond(200, {'content-type': 'text/plain'}, 'hang body');
    };
})();
JS

$t->try_run('no js module')->plan(4);

# --- baseline: normal request works ---
like(http_get('/echo/'), qr/200 OK/, 'echo: normal response');

# --- graceful-shutdown test ---
#
# Fire the hang request in the background (body filter never resolves, so
# the request stays in w->bf_pending).  Then send SIGQUIT and measure how
# long nginx takes to exit.  With the drain fix the worker finalises the
# pending request and exits promptly.

my $pid_file = $t->testdir() . '/nginx.pid';
my $nginx_pid = do { local $/; open my $f, '<', $pid_file or die $!; <$f> };
chomp $nginx_pid;

# Send the hang request in a forked child so we don't block the test process.
my $req_pid = fork();
if ($req_pid == 0) {
    eval { http_get('/hang/') };
    exit 0;
}

# Give the worker a moment to start the body filter and suspend.
select undef, undef, undef, 0.4;

# Send SIGQUIT (graceful shutdown).
kill 'QUIT', $nginx_pid;

# Wait up to 5 seconds for nginx to exit.
my $exited = 0;
for (1 .. 50) {
    $exited = (waitpid($nginx_pid, WNOHANG) != 0);
    last if $exited;
    select undef, undef, undef, 0.1;
}

# Reap the request child.
kill 'TERM', $req_pid;
waitpid($req_pid, 0);

ok($exited, 'nginx exits cleanly within 5 s with pending body-filter async request');

# Prevent Test::Nginx DESTROY from calling stop() on the already-dead server.
$t->{_started} = 0;

# Confirm the drain warning appears — means the pending entry was found.
my $log = $t->read_file('error.log');
like($log, qr/drain body-filter request with error on worker exit/,
     'error log contains body-filter drain warning');

# No open-socket alert — the connection was properly closed by the drain.
unlike($log, qr/\[alert\]/i, 'no open-socket alert in error log');
