#!/usr/bin/perl

# Tests for nginx.describe() — COM mutation safety-class metadata (Layer 1).
#
# Verifies the three classification axes (class / propagation / requestScoped),
# the single-member and full-array forms, null/empty for unknowns, that
# settable() is unchanged, and the drift invariant (every settable() member
# has a describe() entry).
#
# Config-phase checks run in init.js (logged as JSTEST PASS/FAIL).  The
# zoned-vs-nonzoned propagation distinction is checked in a REQUEST handler
# because a zone's shpool is only populated after the worker starts — at
# init_conf time peers->shpool is still NULL for every upstream.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy upstream_zone/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream zoned {
        zone zoned 64k;
        server 127.0.0.1:%%PORT_8091%%;
    }

    upstream plain {
        server 127.0.0.1:%%PORT_8092%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location / { }

        location /p/ {
            proxy_pass http://zoned;
        }

        location /probe/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

var locPath = 'http.servers[0].locations[0]';
var loc     = nginx.http.servers[0].locations[0];

/* --- single-member form: class / reversible / requestScoped --- */
var h = nginx.describe(locPath, 'handler');
check('handler_guarded',      h.class === 'guarded', h.class);
check('handler_not_reqscoped', h.requestScoped === false, h.requestScoped);
check('handler_reversible',   h.reversible === true, h.reversible);
check('handler_type_function', h.type === 'function', h.type);

var root = nginx.describe(locPath, 'root');
check('root_safe',         root.class === 'safe', root.class);
check('root_reqscoped',    root.requestScoped === true, root.requestScoped);
check('root_worker_local', root.propagation === 'worker-local', root.propagation);

/* --- proxy sub-object: pass is guarded + request-scoped --- */
var pp = nginx.describe('http.servers[0].locations[1].proxy', 'pass');
check('proxy_pass_guarded',   pp.class === 'guarded', pp.class);
check('proxy_pass_reqscoped', pp.requestScoped === true, pp.requestScoped);

/* --- headers.addHeaders: array-valued, safe, request-scoped --- */
var ah = nginx.describe('http.servers[0].locations[0].headers', 'addHeaders');
check('addHeaders_safe',     ah.class === 'safe', ah.class);
check('addHeaders_objarray', ah.type === 'object[]', ah.type);

/* --- structural ops: removeLocation is irreversible --- */
var rl = nginx.describe('http.servers[0]', 'removeLocation');
check('removeLocation_irreversible', rl.class === 'irreversible', rl.class);
check('removeLocation_not_reversible', rl.reversible === false, rl.reversible);

/* --- unknown member → null --- */
check('unknown_member_null', nginx.describe(locPath, 'nope') === null);

/* --- unregistered class / primitive → empty array --- */
var ev = nginx.describe('version');   /* a string primitive */
check('unregistered_empty', Array.isArray(ev) && ev.length === 0, ev.length);

/* --- full-array form returns Descriptors with the right shape --- */
var all = nginx.describe(locPath);
check('array_nonempty', Array.isArray(all) && all.length > 10, all.length);
check('array_shape',
      all[0] && typeof all[0].name === 'string'
             && typeof all[0].class === 'string'
             && typeof all[0].propagation === 'string',
      JSON.stringify(all[0]));

/* --- settable() is unchanged (still string[]) --- */
var s = nginx.settable(loc);
check('settable_strings', Array.isArray(s) && typeof s[0] === 'string', typeof s[0]);
check('settable_has_root', s.indexOf('root') >= 0);

/* --- drift invariant: every settable() member has a describe() entry --- */
function driftOk(obj) {
    var names = nginx.settable(obj);
    if (!names.length) { return false; }
    return names.every(function (n) { return nginx.describe(obj, n) !== null; });
}
check('drift_location', driftOk(loc));
check('drift_peer',     driftOk(nginx.http.upstreams[0].peers[0]));

/* --- events sub-object is classified --- */
var ma = nginx.describe('events', 'multiAccept');
check('events_safe', ma && ma.class === 'safe', ma && ma.class);

/* --- request handler: zoned vs non-zoned propagation (post-fork) --- */
var probe = nginx.http.servers[0].locations.find(
                function (l) { return l.path === '/probe/'; });
probe.handler = function (r) {
    var z  = nginx.describe('http.upstreams[0].peers[0]', 'weight');   /* zoned  */
    var nz = nginx.describe('http.upstreams[1].peers[0]', 'weight');   /* plain  */
    r.respond(200, {'Content-Type': 'application/json'},
        JSON.stringify({ zoned: z.propagation, plain: nz.propagation }) + '\n');
};
JS

$t->try_run('no js module or upstream_zone')->plan(20);

# --- Config-phase assertions (error.log) ---
my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS handler_guarded/,        'handler → guarded');
like($log, qr/JSTEST PASS handler_not_reqscoped/,  'handler → requestScoped:false');
like($log, qr/JSTEST PASS handler_reversible/,     'handler → reversible:true');
like($log, qr/JSTEST PASS handler_type_function/,  'handler → type:function');
like($log, qr/JSTEST PASS root_safe/,              'root → safe');
like($log, qr/JSTEST PASS root_reqscoped/,         'root → requestScoped:true');
like($log, qr/JSTEST PASS root_worker_local/,      'root → worker-local');
like($log, qr/JSTEST PASS proxy_pass_guarded/,     'proxy.pass → guarded');
like($log, qr/JSTEST PASS proxy_pass_reqscoped/,   'proxy.pass → requestScoped:true');
like($log, qr/JSTEST PASS addHeaders_safe/,        'headers.addHeaders → safe');
like($log, qr/JSTEST PASS addHeaders_objarray/,    'headers.addHeaders → object[]');
like($log, qr/JSTEST PASS removeLocation_irreversible/, 'removeLocation → irreversible');
like($log, qr/JSTEST PASS removeLocation_not_reversible/, 'removeLocation → reversible:false');
like($log, qr/JSTEST PASS unknown_member_null/,    'unknown member → null');
like($log, qr/JSTEST PASS unregistered_empty/,     'unregistered/primitive → []');
like($log, qr/JSTEST PASS array_shape/,            'descriptor array has correct shape');
like($log, qr/JSTEST PASS settable_has_root/,      'settable() unchanged');
like($log, qr/JSTEST PASS drift_location/,         'drift: every settable loc member described');
like($log, qr/JSTEST PASS drift_peer/,             'drift: every settable peer member described');

# --- Request-phase assertion: zoned-shared vs worker-local ---
my $r = http_get('/probe/');
like($r, qr/"zoned":"zoned-shared".*"plain":"worker-local"/,
     'zoned peer → zoned-shared, plain peer → worker-local (post-fork)');
