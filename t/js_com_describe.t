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

/* --- removeLocation is now guarded + reversible (tombstone, Track L) --- */
var rl = nginx.describe('http.servers[0]', 'removeLocation');
check('removeLocation_guarded', rl.class === 'guarded', rl.class);
check('removeLocation_reversible', rl.reversible === true, rl.reversible);
/* restoreLocation is the inverse and also classified */
var rs = nginx.describe('http.servers[0]', 'restoreLocation');
check('restoreLocation_present', rs && rs.class === 'guarded', rs && rs.class);

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

/* --- follow-up #2: topology methods now classified (close the gap) --- */
/* nginx.http is a plain object, reached via the describe() tag. */
var as = nginx.describe('http', 'addServer');
check('http_addServer_irreversible',
      as && as.class === 'irreversible' && as.reversible === false,
      as && (as.class + '/' + as.reversible));
var at = nginx.describe('http', 'attach');
check('http_attach_irreversible', at && at.class === 'irreversible',
      at && at.class);
var rms = nginx.describe('http', 'removeServer');
check('http_removeServer_guarded',
      rms && rms.class === 'guarded' && rms.reversible === true,
      rms && rms.class);
var rml = nginx.describe('http', 'removeListener');
check('http_removeListener_guarded',
      rml && rml.class === 'guarded' && rml.reversible === true,
      rml && rml.class);
/* the object form (what A4.1 passes) resolves to the same table */
var asObj = nginx.describe(nginx.http, 'addServer');
check('http_addServer_objform', asObj && asObj.class === 'irreversible',
      asObj && asObj.class);
/* the hidden tag must not leak into enumeration */
check('http_tag_hidden',
      Object.keys(nginx.http).indexOf('\xff' + 'ngxDescribeTag') < 0, 'leaked');

/* --- HTTP upstream topology: addPeer guarded + zoned-shared --- */
var ap = nginx.describe('http.upstreams[0]', 'addPeer');
check('upstream_addPeer_guarded', ap && ap.class === 'guarded', ap && ap.class);
check('upstream_addPeer_zoned', ap && ap.propagation === 'zoned-shared',
      ap && ap.propagation);

/* --- cycle.workers is guarded --- */
var cw = nginx.describe('cycle', 'workers');
check('cycle_workers_guarded', cw && cw.class === 'guarded', cw && cw.class);

/* --- follow-up #2: settable() now covers every describe() class --- */
/* server: assignable scalars present, callable methods excluded */
var sSrv = nginx.settable(nginx.http.servers[0]);
check('settable_server_has_root', sSrv.indexOf('root') >= 0, JSON.stringify(sSrv));
check('settable_server_no_method',
      sSrv.indexOf('addLocation') < 0 && sSrv.indexOf('setNames') < 0,
      JSON.stringify(sSrv));
/* cycle: the one assignable scalar */
var sCyc = nginx.settable(nginx.cycle);
check('settable_cycle_workers', sCyc.length === 1 && sCyc[0] === 'workers',
      JSON.stringify(sCyc));
/* nginx.http exposes only methods → no assignable properties */
var sHttp = nginx.settable(nginx.http);
check('settable_http_empty', Array.isArray(sHttp) && sHttp.length === 0,
      JSON.stringify(sHttp));
/* the agreement holds on the newly-covered classes too: settable ⊆ describe */
function driftOk2(obj) {
    var ns = nginx.settable(obj);
    return ns.length > 0 && ns.every(function (n) {
        return nginx.describe(obj, n) !== null; });
}
check('drift_server', driftOk2(nginx.http.servers[0]));
check('drift_cycle',  driftOk2(nginx.cycle));

/* --- discovery root: nginx.describe() with no path → class catalog --- */
var cat = nginx.describe();
check('catalog_is_array', Array.isArray(cat) && cat.length > 30, cat.length);
var catLoc = cat.find(function (e) { return e.class === 'NginxLocation'; });
check('catalog_has_location',
      catLoc && Array.isArray(catLoc.members) && catLoc.members.indexOf('handler') >= 0,
      catLoc && JSON.stringify(catLoc.members).slice(0, 60));

/* --- read-only getters now appear in describe() (complete reference) --- */
var allL = nginx.describe(locPath);
check('describe_includes_readonly_path',
      allL.some(function (d) { return d.name === 'path' && d.class === 'readonly'
                                      && d.access === 'read-only'; }),
      JSON.stringify(allL.map(function (d) { return d.name; })).slice(0, 80));
var pathD = nginx.describe(locPath, 'path');
check('readonly_single_form',
      pathD && pathD.class === 'readonly' && pathD.access === 'read-only'
            && pathD.reversible === false,
      pathD && JSON.stringify(pathD));
/* follow-up #3a: read-only getters carry a static type from the map */
check('readonly_typed', pathD && pathD.type === 'string', pathD && pathD.type);
var hfD = nginx.describe(locPath, 'headerFilters');
check('readonly_typed_array', hfD && hfD.type === 'object[]', hfD && hfD.type);
/* an unmapped read-only accessor still falls back to "getter" honestly */
var proxyD = nginx.describe(locPath, 'proxy');
check('readonly_fallback', proxyD && proxyD.type === 'getter'
                                  && proxyD.class === 'readonly',
      proxyD && JSON.stringify(proxyD));
/* read-only members must NOT leak into settable() */
check('settable_excludes_readonly',
      nginx.settable(loc).indexOf('path') < 0, JSON.stringify(nginx.settable(loc)));

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

$t->try_run('no js module or upstream_zone')->plan(44);

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
like($log, qr/JSTEST PASS removeLocation_guarded/,   'removeLocation → guarded (reversible)');
like($log, qr/JSTEST PASS removeLocation_reversible/,'removeLocation → reversible:true');
like($log, qr/JSTEST PASS restoreLocation_present/,  'restoreLocation classified');
like($log, qr/JSTEST PASS unknown_member_null/,    'unknown member → null');
like($log, qr/JSTEST PASS unregistered_empty/,     'unregistered/primitive → []');
like($log, qr/JSTEST PASS array_shape/,            'descriptor array has correct shape');
like($log, qr/JSTEST PASS settable_has_root/,      'settable() unchanged');
like($log, qr/JSTEST PASS drift_location/,         'drift: every settable loc member described');
like($log, qr/JSTEST PASS drift_peer/,             'drift: every settable peer member described');

# --- follow-up #2: topology-method classification (the closed gap) ---
like($log, qr/JSTEST PASS http_addServer_irreversible/, 'http.addServer → irreversible');
like($log, qr/JSTEST PASS http_attach_irreversible/,    'http.attach → irreversible');
like($log, qr/JSTEST PASS http_removeServer_guarded/,   'http.removeServer → guarded/reversible');
like($log, qr/JSTEST PASS http_removeListener_guarded/, 'http.removeListener → guarded/reversible');
like($log, qr/JSTEST PASS http_addServer_objform/,      'describe(nginx.http, ...) object form');
like($log, qr/JSTEST PASS http_tag_hidden/,             'describe() tag is non-enumerable');
like($log, qr/JSTEST PASS upstream_addPeer_guarded/,    'http upstream addPeer → guarded');
like($log, qr/JSTEST PASS upstream_addPeer_zoned/,      'http upstream addPeer → zoned-shared');
like($log, qr/JSTEST PASS cycle_workers_guarded/,       'cycle.workers → guarded');

# --- follow-up #2: settable() covers the full describe() class set ---
like($log, qr/JSTEST PASS settable_server_has_root/,    'settable(server) has scalars');
like($log, qr/JSTEST PASS settable_server_no_method/,   'settable(server) excludes methods');
like($log, qr/JSTEST PASS settable_cycle_workers/,      'settable(cycle) = [workers]');
like($log, qr/JSTEST PASS settable_http_empty/,         'settable(nginx.http) = [] (methods only)');
like($log, qr/JSTEST PASS drift_server/,                'drift: settable(server) ⊆ describe()');
like($log, qr/JSTEST PASS drift_cycle/,                 'drift: settable(cycle) ⊆ describe()');

# --- discovery root + read-only completeness ---
like($log, qr/JSTEST PASS catalog_is_array/,            'describe() → class catalog');
like($log, qr/JSTEST PASS catalog_has_location/,        'catalog lists NginxLocation + members');
like($log, qr/JSTEST PASS describe_includes_readonly_path/, 'describe() includes read-only path');
like($log, qr/JSTEST PASS readonly_single_form/,        'describe(path,name) classifies read-only getter');
like($log, qr/JSTEST PASS readonly_typed/,              'read-only getter typed from static map (path → string)');
like($log, qr/JSTEST PASS readonly_typed_array/,        'read-only getter typed (headerFilters → object[])');
like($log, qr/JSTEST PASS readonly_fallback/,           'unmapped read-only getter falls back to type:getter');
like($log, qr/JSTEST PASS settable_excludes_readonly/,  'settable() excludes read-only members');

# --- Request-phase assertion: zoned-shared vs worker-local ---
my $r = http_get('/probe/');
like($r, qr/"zoned":"zoned-shared".*"plain":"worker-local"/,
     'zoned peer → zoned-shared, plain peer → worker-local (post-fork)');
