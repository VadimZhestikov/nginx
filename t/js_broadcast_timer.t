#!/usr/bin/perl

# Regression test: nginx.setTimeout() called inside a nginx.broadcast()
# callback must fire.
#
# ngx_js_module (NGX_CORE_MODULE, index 3) runs init_process before
# ngx_event_core_module (index 7), which calls ngx_event_timer_init and
# reinitialises the timer rbtree.  Broadcast callbacks are therefore run
# from ngx_js_http_module.init_process (after the event module), so timers
# and SharedWorker.postMessage() both work correctly there.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /ready/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    /* broadcastDone is set by the setTimeout callback inside broadcast(). */
    nginx.broadcastDone = false;

    nginx.broadcast(function () {
        nginx.setTimeout(0).then(function () {
            nginx.broadcastDone = true;
        });
    });

    var loc = nginx.http.servers[0].locations.find(function (l) {
        return l.path === '/ready/';
    });

    loc.handler = function (req) {
        req.respond(200, {}, String(nginx.broadcastDone));
    };
})();
JS

$t->try_run('no js module')->plan(4);

# Give the timer a moment to fire (it should fire on the first event loop tick).
select undef, undef, undef, 0.2;

my $r1 = http_get('/ready/');
my $r2 = http_get('/ready/');

like($r1, qr/200/, 'broadcast timer: 200 OK');
like($r1, qr/true/, 'broadcast timer: flag set in first worker');
like($r2, qr/200/, 'broadcast timer: second request 200 OK');
like($r2, qr/true/, 'broadcast timer: flag set after second request');

$t->stop();
