#!/usr/bin/perl

# Tests for js_pilgrim_modules/shared_string_map.js
#
# SharedStringMap: synchronous cross-worker string key-value store on SAB.
#
# Covers:
#   - get / set / delete / has / size / clear
#   - tombstone reuse (delete + re-insert same key)
#   - capacity exhaustion (throws when full)
#   - cross-worker visibility (worker_processes 2, 10 sequential reads)
#   - SharedWorker interop (SW writes via its own SSM instance, worker reads)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t   = Test::Nginx->new()->has(qw/http/);
my $dir = $t->testdir();
my $ssm = "$FindBin::Bin/../js_pilgrim_modules/shared_string_map.js";

# QuickJS resolves import paths relative to the importing script.
# Copy the module into the test directory so both init.js and sw.js
# can import it with a simple './shared_string_map.js' specifier.
do {
    local $/;
    open my $fh, '<', $ssm or die "Cannot read $ssm: $!";
    $t->write_file('shared_string_map.js', <$fh>);
};

$t->write_file_expand('nginx.conf', <<EOF);
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /clear/  { }
        location /delete/ { }
        location /full/   { }
        location /get/    { }
        location /has/    { }
        location /set/    { }
        location /size/   { }
        location /sw_get/ { }
        location /sw_set/ { }
    }
}
EOF

# SharedWorker script: receives SAB on first message, builds its own
# SharedStringMap instance from it, then serves {cmd:'set', key, value}
# requests.  Tests that two independent SSM instances backed by the same
# SAB see each other's writes.
$t->write_file('sw.js', <<"JS");
import { SharedStringMap } from './shared_string_map.js';
var map = null;
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        if (msg.data instanceof SharedArrayBuffer) {
            if (!map) {
                map = new SharedStringMap(64, 64, 128, msg.data);
            }
            map.set('sw_color', 'green');
        }
        port.postMessage('done');
    };
};
JS

$t->write_file('init.js', <<"JS");
import { SharedStringMap } from './shared_string_map.js';

var map  = new SharedStringMap(64, 64, 128);  // main test map
var tiny = new SharedStringMap(4, 16, 16);    // capacity-exhaustion map
var sw   = new SharedWorker('$dir/sw.js');

var locs = nginx.http.servers[0].locations;
function loc(p) { return locs.find(function(l) { return l.path === p; }); }

loc('/set/').handler = function(req) {
    map.set(req.headers['x-key'] || 'k', req.headers['x-value'] || 'v');
    req.respond(200, {}, 'ok\\n');
};

loc('/get/').handler = function(req) {
    var v = map.get(req.headers['x-key'] || 'k');
    req.respond(200, {}, (v === undefined ? 'undefined' : v) + '\\n');
};

loc('/delete/').handler = function(req) {
    req.respond(200, {}, String(map.delete(req.headers['x-key'] || 'k')) + '\\n');
};

loc('/has/').handler = function(req) {
    req.respond(200, {}, String(map.has(req.headers['x-key'] || 'k')) + '\\n');
};

loc('/size/').handler = function(req) {
    req.respond(200, {}, String(map.size) + '\\n');
};

loc('/clear/').handler = function(req) {
    map.clear();
    req.respond(200, {}, String(map.size) + '\\n');
};

// capacity = 4; insert 5 distinct keys — 5th must throw
loc('/full/').handler = function(req) {
    tiny.clear();
    var threw = false;
    try {
        for (var i = 0; i < 5; i++) { tiny.set('key' + i, 'val'); }
    } catch(e) { threw = true; }
    req.respond(200, {}, String(threw) + '\\n');
};

// ask the SharedWorker to write 'sw_color'='green' via its own SSM instance
loc('/sw_set/').handler = async function(req) {
    await new Promise(function(resolve) {
        sw.onmessage = function() { resolve(); };
        sw.postMessage(map.sab);
    });
    req.respond(200, {}, 'ok\\n');
};

// read 'sw_color' from the local SSM (which shares the SAB with the SW)
loc('/sw_get/').handler = function(req) {
    var v = map.get('sw_color');
    req.respond(200, {}, (v === undefined ? 'undefined' : v) + '\\n');
};
JS

$t->try_run('no js module')->plan(19);

# ── helpers ───────────────────────────────────────────────────────────────────

sub set_kv {
    my ($k, $v) = @_;
    http(<<"EOF");
GET /set/ HTTP/1.0\r
Host: localhost\r
X-Key: $k\r
X-Value: $v\r
\r
EOF
}

sub get_k {
    my ($k) = @_;
    http(<<"EOF");
GET /get/ HTTP/1.0\r
Host: localhost\r
X-Key: $k\r
\r
EOF
}

# ── set / get ─────────────────────────────────────────────────────────────────

my $r = set_kv('color', 'blue');
like($r, qr{200 OK},   'set: 200 response');
like($r, qr{^ok\b}m,   'set: body ok');

$r = get_k('color');
like($r, qr{200 OK},     'get: 200 response');
like($r, qr{^blue\b}m,   'get: returns stored value');

$r = get_k('no-such-key');
like($r, qr{^undefined}m, 'get: missing key returns undefined');

# ── delete ────────────────────────────────────────────────────────────────────

$r = http(<<"EOF");
GET /delete/ HTTP/1.0\r
Host: localhost\r
X-Key: color\r
\r
EOF
like($r, qr{^true\b}m,    'delete: existing key → true');

$r = get_k('color');
like($r, qr{^undefined}m,  'delete: key gone after delete');

$r = http(<<"EOF");
GET /delete/ HTTP/1.0\r
Host: localhost\r
X-Key: ghost\r
\r
EOF
like($r, qr{^false\b}m,   'delete: missing key → false');

# ── has ───────────────────────────────────────────────────────────────────────

set_kv('flag', 'on');

$r = http(<<"EOF");
GET /has/ HTTP/1.0\r
Host: localhost\r
X-Key: flag\r
\r
EOF
like($r, qr{^true\b}m,   'has: existing key → true');

$r = http(<<"EOF");
GET /has/ HTTP/1.0\r
Host: localhost\r
X-Key: absent\r
\r
EOF
like($r, qr{^false\b}m,  'has: missing key → false');

# ── size / clear ──────────────────────────────────────────────────────────────

$r = http(<<"EOF");
GET /clear/ HTTP/1.0\r
Host: localhost\r
\r
EOF
like($r, qr{^0\b}m, 'clear: size is 0 after clear');

set_kv('sole', 'entry');
$r = http(<<"EOF");
GET /size/ HTTP/1.0\r
Host: localhost\r
\r
EOF
like($r, qr{^1\b}m, 'size: 1 after single insert into empty map');

# ── tombstone reuse ───────────────────────────────────────────────────────────

set_kv('tomb', 'old');
http(<<"EOF");
GET /delete/ HTTP/1.0\r
Host: localhost\r
X-Key: tomb\r
\r
EOF
set_kv('tomb', 'new');
$r = get_k('tomb');
like($r, qr{^new\b}m, 'tombstone: re-insert after delete returns new value');

# ── capacity exhaustion ───────────────────────────────────────────────────────

$r = http(<<"EOF");
GET /full/ HTTP/1.0\r
Host: localhost\r
\r
EOF
like($r, qr{^true\b}m, 'full: inserting beyond capacity throws');

# ── cross-worker visibility ───────────────────────────────────────────────────

# Set a key then fire 10 sequential GETs; with 2 workers both will be
# exercised and must return the same value set by either worker.
set_kv('xw', 'shared');
my @got = map { get_k('xw') } 1..10;
ok(!(grep { $_ !~ /\bshared\b/ } @got),
    'cross-worker: all 10 requests across 2 workers see the stored value');

# ── SharedWorker interop ──────────────────────────────────────────────────────

# The SW receives the SAB and builds its own SSM instance from it.
# SW always writes 'sw_color'='green'; worker reads via its own map instance.
$r = http(<<"EOF");
GET /sw_set/ HTTP/1.0\r
Host: localhost\r
\r
EOF
like($r, qr{200 OK}, 'sw interop: sw_set responds 200');

$r = http(<<"EOF");
GET /sw_get/ HTTP/1.0\r
Host: localhost\r
\r
EOF
like($r, qr{^green\b}m, 'sw interop: worker reads value written by SharedWorker');

# ── auto checks ──────────────────────────────────────────────────────────────

ok($t->waitforsocket('127.0.0.1:' . port(8080)), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
