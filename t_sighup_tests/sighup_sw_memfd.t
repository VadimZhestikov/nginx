#!/usr/bin/perl

# Reload lifecycle leak test: worker-created SAB (memfd) to SharedWorker.
#
# A worker creates a post-fork SharedArrayBuffer (backed by memfd_create),
# sends it to the SharedWorker via postMessage.  The master (SW thread)
# receives the fd via SCM_RIGHTS, mmaps it, then must close the fd.
# If the fd is not closed after each transfer, the master fd count grows
# by 1 per request.  20 reloads × 3 requests each = 60 potential leaks.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use lib '../t/lib';
use Test::Nginx;
use ReloadHarness;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(3);

my $dir = $t->testdir();

$t->write_file('sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        var sab  = msg.data;
        var view = new Int32Array(sab);
        port.postMessage(view[0]);
    };
};
JS

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /sab/   { }
        location /check/ { }
    }
}
EOF

$t->write_file('init.js', <<"JS");
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });

    /*
     * Creates a memfd-backed SAB, sends to SW, SW echoes buf[0].
     * The SW is created dynamically per-request to exercise the
     * dynamic SW channel path on each call.
     */
    set('/sab/', async function(req) {
        var sw   = new SharedWorker('$dir/sw.js');
        var sab  = new SharedArrayBuffer(16);
        var view = new Int32Array(sab);
        view[0]  = 42;

        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage(sab);
        });

        req.respond(200, {'content-type': 'text/plain'}, String(result));
    });
})();
JS

$t->run();

# Baseline: exercise memfd path before measuring.
http_get('/sab/');

my $pid  = master_pid($t);
my $rss0 = rss_kb($pid);
my $fds0 = fd_count($pid);

my $N = 20;
for my $i (1 .. $N) {
    reload_nginx($t, settle => 0.3);
    # Make 3 requests per reload cycle to accumulate any fd leak.
    http_get('/sab/');
    http_get('/sab/');
    http_get('/sab/');
}

my $rss1 = rss_kb($pid);
my $fds1 = fd_count($pid);

assert_rss_stable($rss0, $rss1, $N, 'memfd SAB RSS');
assert_fd_stable($fds0, $fds1, $N, 'memfd SAB fd (SCM_RIGHTS cleanup)');
like(http_get('/check/'), qr/200 OK/, 'nginx still serving after reloads');
