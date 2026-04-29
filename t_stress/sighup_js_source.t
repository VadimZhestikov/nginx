#!/usr/bin/perl

# Reload lifecycle leak test: JS runtime init/destroy cycle.
#
# Each SIGHUP triggers: parse config → create JS runtime → evaluate
# js_source → fork workers → exit old workers → exit old runtime.
# If the old JSRuntime is not freed on exit_master, or if js_source
# allocations are not cleaned up, RSS grows linearly with reload count.
#
# N=20 reload cycles with a js_source that reads COM properties.

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

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.2:%%PORT_8091%% backup;
    }

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /check/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    /* Read a variety of COM properties to exercise the runtime. */
    void nginx.version;
    void nginx.cpu_count;
    void nginx.cycle.hostname;

    var srvs = nginx.http.servers;
    for (var i = 0; i < srvs.length; i++) {
        void srvs[i].name;
        var locs = srvs[i].locations;
        for (var j = 0; j < locs.length; j++) {
            void locs[j].path;
        }
    }

    var ups = nginx.http.upstreams;
    for (var i = 0; i < ups.length; i++) {
        void ups[i].name;
        var peers = ups[i].peers;
        for (var j = 0; j < peers.length; j++) {
            void peers[j].address;
            void peers[j].weight;
        }
    }

    nginx.http.servers[0].locations
        .find(function(l) { return l.path === '/check/'; })
        .handler = function(req) {
            req.respond(200, {'content-type': 'text/plain'}, 'ok');
        };
})();
JS

$t->run();

my $pid  = master_pid($t);
my $rss0 = rss_kb($pid);
my $fds0 = fd_count($pid);

my $N = 20;
for my $i (1 .. $N) {
    reload_nginx($t);
}

my $rss1 = rss_kb($pid);
my $fds1 = fd_count($pid);

assert_rss_stable($rss0, $rss1, $N, 'JS runtime RSS');
assert_fd_stable($fds0, $fds1, $N, 'JS runtime fd');
like(http_get('/check/'), qr/200 OK/, 'nginx still serving after reloads');
