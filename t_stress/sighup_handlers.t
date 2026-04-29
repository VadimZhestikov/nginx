#!/usr/bin/perl

# Reload lifecycle leak test: location.handler JSValue across reloads.
#
# Each reload evaluates js_source which calls location.handler = fn.
# Before the handler-replacement fix, __ngx_handlers__[] grew without
# bound within a single nginx run.  This test verifies the RELOAD path:
# the old handler JSValue must be freed when the JS runtime is torn down
# on exit_master, and the new runtime starts clean.
#
# RSS growth here comes from the JS runtime lifecycle, not from handler
# accumulation (each reload is a fresh runtime).  The fd check confirms
# no file-descriptor sideffects from repeated handler wiring.

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

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /a/     { }
        location /b/     { }
        location /c/     { }
        location /check/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        var l = locs.find(function(l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* Assign handlers to several locations; exercise __ngx_handlers__[]. */
    set('/a/', function(req) { req.respond(200, {}, 'a'); });
    set('/b/', function(req) { req.respond(200, {}, 'b'); });
    set('/c/', function(req) { req.respond(200, {}, 'c'); });
    set('/check/', function(req) {
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });
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

assert_rss_stable($rss0, $rss1, $N, 'handler reload RSS');
assert_fd_stable($fds0, $fds1, $N, 'handler reload fd');
like(http_get('/check/'), qr/200 OK/, 'nginx still serving after reloads');
