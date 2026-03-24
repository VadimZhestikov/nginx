#!/usr/bin/perl

# Tests for nginx.get(path), nginx.set(path, value), nginx.settable(obj):
#   get     — read upstream peer weight by path
#   set     — write upstream peer weight by path
#   settable — returns array of settable property names for a peer

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

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.2:%%PORT_8091%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /get/     { }
        location /set/     { }
        location /settable/{ }
        location /root/    { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(n) { nginx.log(6, 'JSTEST PASS ' + n); }
function fail(n, v) { nginx.log(6, 'JSTEST FAIL ' + n + ': ' + String(v)); }
function check(n, ok, v) { ok ? pass(n) : fail(n, v); }

var locs = nginx.http.servers[0].locations;

/* -- nginx.get -- */

/* 1. Read upstream peer weight via path */
var w0 = nginx.get('http.upstreams[0].peers[0].weight');
check('get_peer_weight', w0 === 1, w0);

/* 2. Read location root via path */
var rootVal = nginx.get('http.servers[0].locations[0].root');
check('get_location_root', typeof rootVal === 'string' && rootVal.length > 0,
      rootVal);

/* -- nginx.set -- */

/* 3. Write upstream peer weight via path */
nginx.set('http.upstreams[0].peers[0].weight', 7);
var w1 = nginx.http.upstreams[0].peers[0].weight;
check('set_peer_weight', w1 === 7, w1);

/* 4. Read it back via nginx.get */
var w2 = nginx.get('http.upstreams[0].peers[0].weight');
check('get_after_set', w2 === 7, w2);

/* -- nginx.settable -- */

/* 5. settable on a peer object returns non-empty array */
var peer = nginx.http.upstreams[0].peers[0];
var props = nginx.settable(peer);
check('settable_peer_is_array', Array.isArray(props), props);
check('settable_peer_has_weight', props.indexOf('weight') >= 0,
      props.join(','));

/* 6. settable on a path string */
var locProps = nginx.settable('http.servers[0].locations[0]');
check('settable_loc_is_array', Array.isArray(locProps), locProps);
check('settable_loc_has_root', locProps.indexOf('root') >= 0,
      locProps.join(','));

/* Install request handlers for live tests */
var getLoc = locs.find(function(l) { return l.path === '/get/'; });
var setLoc = locs.find(function(l) { return l.path === '/set/'; });

getLoc.handler = function(req) {
    var v = nginx.get('http.upstreams[0].peers[0].weight');
    req.respond(200, {'Content-Type': 'text/plain'}, String(v) + '\n');
};

setLoc.handler = function(req) {
    nginx.set('http.upstreams[0].peers[0].weight', 99);
    req.respond(200, {}, 'ok\n');
};
JS

$t->try_run('no js module')->plan(10);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS get_peer_weight/,  'nginx.get reads peer weight');
like($log, qr/JSTEST PASS get_location_root/,'nginx.get reads location root');
like($log, qr/JSTEST PASS set_peer_weight/,  'nginx.set writes peer weight');
like($log, qr/JSTEST PASS get_after_set/,    'nginx.get reads updated value');
like($log, qr/JSTEST PASS settable_peer_is_array/, 'settable(peer) is array');
like($log, qr/JSTEST PASS settable_peer_has_weight/, 'settable(peer) has weight');
like($log, qr/JSTEST PASS settable_loc_is_array/,  'settable(loc) is array');
like($log, qr/JSTEST PASS settable_loc_has_root/,  'settable(loc) has root');

# Request-phase tests
my $r1 = http_get('/get/');
like($r1, qr/\b7\b/, 'nginx.get in request handler returns current value');

http_get('/set/');
my $r2 = http_get('/get/');
like($r2, qr/\b99\b/, 'nginx.set in request handler persists across requests');

$t->stop();
