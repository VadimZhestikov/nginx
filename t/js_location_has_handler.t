#!/usr/bin/perl

# Tests for location.hasHandler (bool r/o):
#   false  — no JS handler installed (fresh location)
#   true   — after location.handler = fn
#   false  — after location.clearHandler()
#   recycle — set → clear → set: hasHandler tracks each transition
#   request  — hasHandler readable from inside a request handler

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

        location /a/ { }
        location /b/ { }
        location /c/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(n) { nginx.log(6, 'JSTEST PASS ' + n); }
function fail(n, v) { nginx.log(6, 'JSTEST FAIL ' + n + ': ' + String(v)); }
function check(n, ok, v) { ok ? pass(n) : fail(n, v); }

var locs = nginx.http.servers[0].locations;
var a = locs.find(function(l) { return l.path === '/a/'; });
var b = locs.find(function(l) { return l.path === '/b/'; });
var c = locs.find(function(l) { return l.path === '/c/'; });

/* 1. Fresh location has no handler */
check('a_false_initially', a.hasHandler === false, a.hasHandler);

/* 2. After assigning a handler, hasHandler becomes true */
a.handler = function(req) { req.respond(200, {}, 'a'); };
check('a_true_after_set', a.hasHandler === true, a.hasHandler);

/* 3. After clearHandler(), hasHandler is false again */
a.clearHandler();
check('a_false_after_clear', a.hasHandler === false, a.hasHandler);

/* 4. set → clear → set: hasHandler tracks correctly */
b.handler = function(req) { req.respond(200, {}, 'b1'); };
check('b_true_first',  b.hasHandler === true, b.hasHandler);
b.clearHandler();
check('b_false_cleared', b.hasHandler === false, b.hasHandler);
b.handler = function(req) { req.respond(200, {}, 'b2'); };
check('b_true_second', b.hasHandler === true, b.hasHandler);

/* 5. Install handler on /c/ — readable from request handler */
c.handler = function(req) {
    var selfHas = req.location.hasHandler;
    req.respond(200, {'Content-Type': 'text/plain'},
                String(selfHas));
};
JS

$t->try_run('no js module')->plan(7);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS a_false_initially/, 'hasHandler false on fresh location');
like($log, qr/JSTEST PASS a_true_after_set/,  'hasHandler true after handler=fn');
like($log, qr/JSTEST PASS a_false_after_clear/,'hasHandler false after clearHandler');
like($log, qr/JSTEST PASS b_true_first/,       'recycle: true after first set');
like($log, qr/JSTEST PASS b_false_cleared/,    'recycle: false after clear');
like($log, qr/JSTEST PASS b_true_second/,      'recycle: true after second set');

# Request-phase: hasHandler readable from req.location inside the handler
my $r = http_get('/c/');
like($r, qr/\btrue\b/, 'hasHandler true when read from request handler');

$t->stop();
