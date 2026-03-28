#!/usr/bin/perl

# Tests for JS-Pilgrim P11: nginx.shared — cross-worker shared key/value store.
#
# nginx.shared provides a plain object with five methods backed by a fixed
# shared-memory zone:
#   get(key)           → string | undefined
#   set(key, val)      → undefined
#   delete(key)        → boolean
#   keys()             → string[]
#   incr(key[, delta]) → number

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(18);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p11_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /set/    { }
        location /get/    { }
        location /delete/ { }
        location /keys/   { }
        location /incr/   { }
    }
}
EOF

# Location alphabetical order:
#   locs[0] = /delete/
#   locs[1] = /get/
#   locs[2] = /incr/
#   locs[3] = /keys/
#   locs[4] = /set/

$t->write_file_expand('p11_init.js', <<'JS');
// JS-Pilgrim P11 — nginx.shared cross-worker key/value store tests

var locs = nginx.http.servers[0].locations;

// ----------------------------------------------------------------
// /set/ (locs[4]): write a key, respond with its value
// ----------------------------------------------------------------
locs[4].handler = function(req) {
    var k = req.headers['x-key']   || 'k';
    var v = req.headers['x-value'] || 'v';
    nginx.shared.set(k, v);
    req.respond(200, {}, 'ok\n');
};

// ----------------------------------------------------------------
// /get/ (locs[1]): read a key
// ----------------------------------------------------------------
locs[1].handler = function(req) {
    var k = req.headers['x-key'] || 'k';
    var v = nginx.shared.get(k);
    req.respond(200, {}, (v === undefined ? 'undefined' : v) + '\n');
};

// ----------------------------------------------------------------
// /delete/ (locs[0]): delete a key, respond true/false
// ----------------------------------------------------------------
locs[0].handler = function(req) {
    var k = req.headers['x-key'] || 'k';
    var deleted = nginx.shared.delete(k);
    req.respond(200, {}, String(deleted) + '\n');
};

// ----------------------------------------------------------------
// /keys/ (locs[3]): list all keys as comma-separated
// ----------------------------------------------------------------
locs[3].handler = function(req) {
    var ks = nginx.shared.keys();
    req.respond(200, {}, ks.sort().join(',') + '\n');
};

// ----------------------------------------------------------------
// /incr/ (locs[2]): incr a counter, respond with new value
// ----------------------------------------------------------------
locs[2].handler = function(req) {
    var k     = req.headers['x-key']   || 'counter';
    var delta = parseInt(req.headers['x-delta'] || '1', 10);
    var v = nginx.shared.incr(k, delta);
    req.respond(200, {}, String(v) + '\n');
};
JS

$t->run();

# -----------------------------------------------------------------------
# 1–2: set a key, then get it back
# -----------------------------------------------------------------------

my $r = http(<<EOF);
GET /set/ HTTP/1.0\r
Host: localhost\r
X-Key: hello\r
X-Value: world\r
\r
EOF
like($r, qr{200 OK},  'set: response 200');
like($r, qr{^ok}m,    'set: body ok');

$r = http(<<EOF);
GET /get/ HTTP/1.0\r
Host: localhost\r
X-Key: hello\r
\r
EOF
like($r, qr{200 OK},    'get: response 200');
like($r, qr{^world}m,   'get: value matches what was set');

# -----------------------------------------------------------------------
# 5: get a key that was never set — should return "undefined"
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /get/ HTTP/1.0\r
Host: localhost\r
X-Key: no-such-key\r
\r
EOF
like($r, qr{^undefined}m, 'get: missing key returns undefined');

# -----------------------------------------------------------------------
# 6–7: delete an existing key
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /delete/ HTTP/1.0\r
Host: localhost\r
X-Key: hello\r
\r
EOF
like($r, qr{200 OK},  'delete: response 200');
like($r, qr{^true}m,  'delete: existing key returns true');

# verify it is gone
$r = http(<<EOF);
GET /get/ HTTP/1.0\r
Host: localhost\r
X-Key: hello\r
\r
EOF
like($r, qr{^undefined}m, 'delete: key is gone after delete');

# -----------------------------------------------------------------------
# 9: delete a non-existent key returns false
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /delete/ HTTP/1.0\r
Host: localhost\r
X-Key: ghost\r
\r
EOF
like($r, qr{^false}m, 'delete: missing key returns false');

# -----------------------------------------------------------------------
# 10–12: keys() lists only currently present keys
# -----------------------------------------------------------------------

# Store two keys
http(<<EOF);
GET /set/ HTTP/1.0\r
Host: localhost\r
X-Key: alpha\r
X-Value: 1\r
\r
EOF

http(<<EOF);
GET /set/ HTTP/1.0\r
Host: localhost\r
X-Key: beta\r
X-Value: 2\r
\r
EOF

$r = http(<<EOF);
GET /keys/ HTTP/1.0\r
Host: localhost\r
\r
EOF
like($r, qr{200 OK},         'keys: response 200');
like($r, qr{alpha},          'keys: contains alpha');
like($r, qr{beta},           'keys: contains beta');

# -----------------------------------------------------------------------
# 13–14: incr creates a counter and increments it
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /incr/ HTTP/1.0\r
Host: localhost\r
X-Key: cnt\r
X-Delta: 1\r
\r
EOF
like($r, qr{200 OK},  'incr: response 200');
like($r, qr{^1}m,     'incr: first call → 1');

$r = http(<<EOF);
GET /incr/ HTTP/1.0\r
Host: localhost\r
X-Key: cnt\r
X-Delta: 5\r
\r
EOF
like($r, qr{^6}m, 'incr: second call with delta 5 → 6');

# -----------------------------------------------------------------------
# 15–16: incr with negative delta (decrement)
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /incr/ HTTP/1.0\r
Host: localhost\r
X-Key: cnt\r
X-Delta: -3\r
\r
EOF
like($r, qr{^3}m, 'incr: negative delta decrements (6 - 3 = 3)');

# -----------------------------------------------------------------------
# 17–18: Auto checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
