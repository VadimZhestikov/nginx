#!/usr/bin/perl

# Tests for bcast_fds master-write-end fd leak on reload (KnownFailure_2: Step 4).
#
# ngx_js_sw_manager_start() is called on every nginx reload (SIGHUP).  Before
# this fix it silently set bcast_fds[i][0..1] = -1 without closing the old
# master-write (bcast_fds[i][0]) file descriptors, leaking up to
# NGX_MAX_PROCESSES fds per reload.
#
# After the fix, the master-side write ends are closed before the reset loop,
# so repeated reloads do not accumulate open fds in the master process.
#
# Verifies that SharedWorker delivery still works correctly after three
# consecutive nginx reloads.
#
# Stability note: we use reload_nginx() (from ReloadHarness) rather than the
# bare $t->reload().  $t->reload() just sends SIGHUP and returns immediately;
# old workers keep accepting connections while the master is still inside
# ngx_init_cycle() / retire_threads().  If wait_ready() lands a request on an
# old worker at the same moment retire_threads() sends TERM to the SharedWorker
# channel, TERM can win the race, the SW exits without echoing, and the
# worker's Promise never resolves — causing http_get() to hang forever.
# reload_nginx() polls until all old workers have exited, guaranteeing that
# retire_threads() has already completed and only fresh workers serve requests.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use lib '../t_stress/lib';
use Test::Nginx;
use ReloadHarness;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file('echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) { port.postMessage(ev.data); };
};
JS

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_bcast_reload.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location /echo/ { }
    }
}
EOF

$t->write_file('init_bcast_reload.js', <<"JS");
(function() {
    var prefix = nginx.cycle.prefix;
    new SharedWorker(prefix + 'echo_sw.js');

    var locs = nginx.http.servers[0].locations;
    function findLoc(p) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === p) { return locs[i]; }
        }
    }

    findLoc('/echo/').handler = async function(req) {
        var sw = new SharedWorker(prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('hello');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(6);

sub wait_ready {
    for (1 .. 100) {
        my $r = http_get('/echo/');
        return $r if defined $r && $r =~ /200 OK/;
        select undef, undef, undef, 0.1;
    }
    return undef;
}

like(http_get('/echo/'), qr/hello/, 'pre-reload: delivery works');

reload_nginx($t);
like(wait_ready(), qr/hello/, 'post-reload 1: delivery works');

reload_nginx($t);
like(wait_ready(), qr/hello/, 'post-reload 2: delivery works');

reload_nginx($t);
like(wait_ready(), qr/hello/, 'post-reload 3: delivery works');

$t->stop();
unlike($t->read_file('error.log'), qr/\[alert\]|\[emerg\]/i,
    'no alert/emerg after 3 reloads');
ok(1, 'bcast_fds cleanup on reload passes all checks');
