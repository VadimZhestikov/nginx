#!/usr/bin/perl

# Reload lifecycle leak test: SharedWorker thread + socketpair fd lifecycle.
#
# Each SIGHUP should retire the old SW pthread and its per-worker
# SOCK_SEQPACKET socketpairs, then start fresh ones.  If retire does
# not close both ends of every channel, the master fd count grows by
# 2×worker_processes per reload.  With N=15 reloads and 2 workers,
# an undetected leak of 1 socketpair end = 30 extra fds.

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
        port.postMessage('echo:' + msg.data);
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

        location /check/ { }
        location /ping/  { }
    }
}
EOF

$t->write_file('init.js', <<"JS");
(function() {
    var sw   = new SharedWorker('$dir/sw.js');
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });

    set('/ping/', async function(req) {
        var reply = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('ping');
        });
        req.respond(200, {'content-type': 'text/plain'}, reply);
    });
})();
JS

$t->run();

# Verify SW works before starting reload loop.
http_get('/ping/');

my $pid  = master_pid($t);
my $rss0 = rss_kb($pid);
my $fds0 = fd_count($pid);

my $N = 15;
for my $i (1 .. $N) {
    reload_nginx($t);
}

my $rss1 = rss_kb($pid);
my $fds1 = fd_count($pid);

assert_rss_stable($rss0, $rss1, $N, 'SW RSS');
assert_fd_stable($fds0, $fds1, $N, 'SW socketpair fd');
like(http_get('/check/'), qr/200 OK/, 'nginx still serving after reloads');
