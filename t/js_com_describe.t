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
/* An unmapped read-only accessor must still fall back to type:"getter" —
 * the honest "I don't know" path, which is the thing under test.
 * This used to hardcode `proxy`, and broke the moment `proxy` was classified
 * as handle<NginxProxy> (M4 namespace typing: a static check has no prototype
 * to walk, so the sub-object accessors had to move into the table). Pick the
 * accessor by its SYMPTOM instead of by name, so typing one more member
 * refines the registry without falsifying this check. */
var fbD = null, fbWhere = '';
[['location', loc], ['server', nginx.http.servers[0]], ['http', nginx.http],
 ['upstream', nginx.http.upstreams[0]], ['peer', nginx.http.upstreams[0].peers[0]],
 ['proxy', loc.proxy], ['headers', loc.headers]]
    .forEach(function (pair) {
        if (fbD) { return; }
        var ms;
        try { ms = nginx.describe(pair[1]); } catch (e) { return; }
        var hit = ms.filter(function (d) { return d.type === 'getter'; })[0];
        if (hit) { fbD = hit; fbWhere = pair[0] + '.' + hit.name; }
    });
check('readonly_fallback', !!(fbD && fbD.class === 'readonly'),
      fbD ? fbWhere + ' ' + JSON.stringify(fbD)
          : 'no untyped accessor left on location/server/http — the fallback is '
            + 'unreachable from here, so this check has stopped exercising it; '
            + 'point it at a class that still has one, or retire it');
/* and the newly classified accessor reports its handle type, not "getter" */
var proxyD = nginx.describe(locPath, 'proxy');
check('readonly_wrapper_typed',
      proxyD && proxyD.type === 'handle<NginxProxy>'
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
/* --- M2b: typed signatures ------------------------------------------- */
/* addHook / addResponseHook were previously ABSENT from the location table,
 * so describe() did not report them at all. */
var ah2 = nginx.describe(locPath, 'addHook');
check('sig_addHook_described', ah2 !== null && ah2 !== undefined, ah2);
check('sig_addHook_params',
      ah2 && ah2.params && ah2.params.length === 1 &&
      ah2.params[0].name === 'fn' &&
      ah2.params[0].type === 'handle<Function>' &&
      ah2.params[0].optional === false,
      ah2 && JSON.stringify(ah2.params));
check('sig_addHook_returns', ah2 && ah2.returns === 'void', ah2 && ah2.returns);
check('sig_addHook_effects',
      ah2 && ah2.effects && ah2.effects[0] === 'register.hook.precontent',
      ah2 && JSON.stringify(ah2.effects));

var arh = nginx.describe(locPath, 'addResponseHook');
check('sig_addResponseHook_described', arh !== null && arh !== undefined, arh);

/* optional param + union type + bool return */
var rl = nginx.describe(locPath, 'removeLocation');
check('sig_removeLocation_optional',
      rl && rl.params && rl.params.length === 2 && rl.params[1].optional === true,
      rl && JSON.stringify(rl.params));
check('sig_removeLocation_union',
      rl && rl.params[0].type.indexOf('str|handle<') === 0, rl && rl.params[0].type);
check('sig_removeLocation_returns_bool', rl && rl.returns === 'bool', rl && rl.returns);

/* string ownership ABI is recorded */
check('sig_mem_borrowed', rl && rl.params[0].mem === 'borrowed', rl && rl.params[0].mem);

/* ADDITIVE: an untyped member must be byte-for-byte the old 8-key Descriptor */
check('sig_untyped_unchanged',
      root.params === undefined && root.returns === undefined &&
      root.effects === undefined,
      JSON.stringify(Object.keys(root)));

/* server methods reuse the location signatures (same ngx_js_do_* helpers) */
var srvAL = nginx.describe('http.servers[0]', 'addLocation');
check('sig_server_addLocation_typed',
      srvAL && srvAL.returns === 'handle<NginxLocation>' &&
      srvAL.params && srvAL.params.length === 1,
      srvAL && JSON.stringify(srvAL));

/* M2d: further tranches — params derived from the implementations */
var ahdr = nginx.describe('http.servers[0].locations[0].headers', 'addHeader');
check('sig_addHeader_two_params',
      ahdr && ahdr.params && ahdr.params.length === 2 &&
      ahdr.params[0].name === 'name' && ahdr.params[1].name === 'value' &&
      ahdr.returns === 'void',
      ahdr && JSON.stringify(ahdr));

var apeer = nginx.describe('http.upstreams[0]', 'addPeer');
check('sig_addPeer_record',
      apeer && apeer.params && apeer.params.length === 1 &&
      apeer.params[0].type === 'record' && apeer.returns === 'void',
      apeer && JSON.stringify(apeer));

/* --- `callable`: method vs assignable slot --------------------------------
 * type:"function" alone cannot tell addHook() (call it) from handler (assign
 * to it); the registry knows, so describe() now says so. */
check('callable_method_true',  ah2 && ah2.callable === true,  ah2 && ah2.callable);
check('callable_slot_false',   h.callable === false,          h.callable);
var roDesc = nginx.describe(locPath, 'path');
check('callable_readonly_false',
      roDesc && roDesc.callable === false, roDesc && roDesc.callable);

/* --- M2e: the structural operators on nginx.http --------------------------- */
var has = nginx.describe('http', 'addServer');
check('sig_http_addServer_typed',
      has && has.params && has.params.length === 2 &&
      has.params[0].type === 'str' && has.params[1].optional === true &&
      has.returns === 'handle<NginxServer>',
      has && JSON.stringify(has));

var hat = nginx.describe('http', 'attach');
check('sig_http_attach_returns_listener',
      hat && hat.returns === 'handle<NginxHttpListener>' &&
      hat.params.length === 1 &&
      hat.params[0].type === 'handle<NginxSocket>',
      hat && JSON.stringify(hat));

/* restoreServer is typed 1..1 even though the wrapper forwards a 2-slot argv
 * and silently swallows a second argument — the contract, not the tolerance. */
var hrs = nginx.describe('http', 'restoreServer');
check('sig_restoreServer_strict_arity',
      hrs && hrs.params && hrs.params.length === 1, hrs && JSON.stringify(hrs));

/* location.clone() had NO row at all until M2e */
var lclone = nginx.describe(locPath, 'clone');
check('location_clone_classified',
      lclone && lclone.callable === true && lclone.class === 'guarded' &&
      lclone.returns === 'handle<NginxLocation>',
      lclone && JSON.stringify(lclone));

/* server.clone() was SAFE + reversible, but it commits a cscf into cycle->pool
 * AND splices it into every vhost dispatch entry — live, routable, permanent. */
var sclone = nginx.describe('http.servers[0]', 'clone');
check('server_clone_irreversible',
      sclone && sclone.class === 'irreversible' && sclone.reversible === false,
      sclone && JSON.stringify(sclone));

/* --- coverage guards: walk the live COM tree ------------------------------
 * Completeness comes from WALKING, not from a hand-written class list — a new
 * class reachable from nginx is covered the day it is added.  COM wrappers are
 * rebuilt per access, so identity-based cycle detection does not work; the walk
 * is bounded by depth and node count instead.
 *
 * This walk is also what found the config-phase SIGSEGV in location.charset:
 * nothing else had ever touched every getter at config time.
 */
function protoMethods(o) {
    var out = [], seen = {}, p;
    /* OWN function-valued properties count too: several COM nodes (nginx.http,
     * the cycle socket entries) are plain objects with their methods attached
     * directly, so a prototype-only scan silently misses them. */
    var ons;
    try { ons = Object.getOwnPropertyNames(o); } catch (e) { ons = []; }
    for (var oi = 0; oi < ons.length; oi++) {
        var on = ons[oi];
        if (seen[on]) continue;
        var od;
        try { od = Object.getOwnPropertyDescriptor(o, on); } catch (e) { continue; }
        if (od && typeof od.value === 'function') { seen[on] = 1; out.push(on); }
    }
    try { p = Object.getPrototypeOf(o); } catch (e) { return out; }
    while (p && p !== Object.prototype) {
        var ns;
        try { ns = Object.getOwnPropertyNames(p); } catch (e) { break; }
        for (var i = 0; i < ns.length; i++) {
            var n = ns[i];
            if (n === 'constructor' || seen[n]) continue;
            seen[n] = 1;
            var pd;
            try { pd = Object.getOwnPropertyDescriptor(p, n); } catch (e) { continue; }
            if (pd && typeof pd.value === 'function') out.push(n);
        }
        p = Object.getPrototypeOf(p);
    }
    return out;
}

var noSig = [], noRow = {}, walked = 0;

function walkCom(obj, path, depth) {
    if (depth > 6 || walked > 300 || !obj || typeof obj !== 'object') return;
    walked++;

    var ds;
    try { ds = nginx.describe(obj); } catch (e) { return; }
    if (!ds || typeof ds.length !== 'number') return;

    var byName = {}, k;
    for (k = 0; k < ds.length; k++) {
        byName[ds[k].name] = 1;
        if (ds[k].callable === true && ds[k].params === undefined) {
            noSig.push(path + '.' + ds[k].name);
        }
    }

    /* Plain Arrays returned by COM getters carry all of Array.prototype and
     * are not COM classes — skip, or the signal drowns in map/filter/at. */
    if (!Array.isArray(obj)) {
        var ms = protoMethods(obj);
        for (k = 0; k < ms.length; k++) {
            if (!byName[ms[k]]) noRow[ms[k]] = 1;
        }
    }

    /* describe() reports classified members plus prototype getters only, so
     * plain-object nodes (nginx.http) hide their children from it. */
    var names = {};
    for (k = 0; k < ds.length; k++)
        if (ds[k].callable !== true) names[ds[k].name] = 1;
    var own;
    try { own = Object.keys(obj); } catch (e) { own = []; }
    for (k = 0; k < own.length; k++) names[own[k]] = 1;

    for (var nm in names) {
        var v;
        try { v = obj[nm]; } catch (e) { continue; }
        if (!v || typeof v !== 'object') continue;
        if (typeof v.length === 'number' && v.length > 0
            && typeof v[0] === 'object')
        {
            for (var j = 0; j < v.length && j < 2; j++)
                walkCom(v[j], path + '.' + nm + '[' + j + ']', depth + 1);
        } else {
            walkCom(v, path + '.' + nm, depth + 1);
        }
    }
}

walkCom(nginx.http, 'http', 0);
try { walkCom(nginx.cycle, 'cycle', 0); } catch (e) { }

check('walk_reached_tree', walked > 100, 'walked=' + walked);

/* GUARD 1 — every classified method carries a signature.  Holds today; this
 * is what stops the next method row from landing untyped. */
check('walk_every_callable_typed', noSig.length === 0, noSig.sort().join(' '));

/* GUARD 2 — every COM method is classified.  Was a ratchet over a pinned
 * backlog of 18 names; M2f classified all of them, so this is now the
 * absolute invariant: describe() knows about every callable COM member.
 *
 * SCOPE: this walks the COM CONFIGURATION tree.  The request object, the L4
 * connection object and the Worker/SharedWorker ports are deliberately NOT
 * classified -- describe()'s axes (how a change reaches other workers,
 * reversibility, request-scoping) are meaningless for req.respond(), so they
 * are not COM members and the walk never reaches them. */
check('walk_every_method_classified',
      Object.keys(noRow).length === 0,
      Object.keys(noRow).sort().join(' '))

JS

$t->try_run('no js module or upstream_zone')->plan(69);

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
like($log, qr/JSTEST PASS readonly_fallback/,           'unmapped read-only getter falls back honestly');
like($log, qr/JSTEST PASS readonly_wrapper_typed/,     'classified sub-object accessor reports handle<NginxProxy>');
like($log, qr/JSTEST PASS settable_excludes_readonly/,  'settable() excludes read-only members');

# --- Request-phase assertion: zoned-shared vs worker-local ---
my $r = http_get('/probe/');
like($r, qr/"zoned":"zoned-shared".*"plain":"worker-local"/,
     'zoned peer → zoned-shared, plain peer → worker-local (post-fork)');

# --- M2b: typed signatures ---
like($log, qr/JSTEST PASS sig_addHook_described/,      'addHook is now classified (was missing from the table)');
like($log, qr/JSTEST PASS sig_addHook_params/,         'addHook signature: params typed');
like($log, qr/JSTEST PASS sig_addHook_returns/,        'addHook signature: returns void');
like($log, qr/JSTEST PASS sig_addHook_effects/,        'addHook signature: effects recorded');
like($log, qr/JSTEST PASS sig_addResponseHook_described/, 'addResponseHook is now classified');
like($log, qr/JSTEST PASS sig_removeLocation_optional/, 'removeLocation: optional opts param');
like($log, qr/JSTEST PASS sig_removeLocation_union/,    'removeLocation: union key type (str|handle)');
like($log, qr/JSTEST PASS sig_removeLocation_returns_bool/, 'removeLocation: returns bool');
like($log, qr/JSTEST PASS sig_mem_borrowed/,            'string ownership ABI recorded (mem:borrowed)');
like($log, qr/JSTEST PASS sig_untyped_unchanged/,       'untyped member keeps the original 8-key Descriptor');
like($log, qr/JSTEST PASS sig_server_addLocation_typed/, 'server.addLocation shares the location signature');
like($log, qr/JSTEST PASS sig_addHeader_two_params/, 'headers.addHeader typed (2 params, void)');
like($log, qr/JSTEST PASS sig_addPeer_record/, 'upstream.addPeer typed (record param)');

# --- `callable`: a method you call vs a slot you assign ---
like($log, qr/JSTEST PASS callable_method_true/,   'addHook is callable:true');
like($log, qr/JSTEST PASS callable_slot_false/,    'handler is a slot, callable:false');
like($log, qr/JSTEST PASS callable_readonly_false/,'read-only descriptor carries callable:false');

# --- M2e: the structural operators ---
like($log, qr/JSTEST PASS sig_http_addServer_typed/,
     'http.addServer typed (str + optional record → handle<NginxServer>)');
like($log, qr/JSTEST PASS sig_http_attach_returns_listener/,
     'http.attach typed (socket handle → listener handle)');
like($log, qr/JSTEST PASS sig_restoreServer_strict_arity/,
     'restoreServer typed to its contract (1..1), not its tolerance');
like($log, qr/JSTEST PASS location_clone_classified/,
     'location.clone() is classified at all (had no row before M2e)');
like($log, qr/JSTEST PASS server_clone_irreversible/,
     'server.clone() reclassified SAFE → irreversible (commits a routable cscf)');

# --- coverage guards over a live walk of the COM tree ---
like($log, qr/JSTEST PASS walk_reached_tree/,
     'COM tree walk reaches the tree (and no longer SIGSEGVs on charset)');
like($log, qr/JSTEST PASS walk_every_callable_typed/,
     'every classified method carries a signature');
like($log, qr/JSTEST PASS walk_every_method_classified/,
     'every COM method is classified (M2f closed the last 18)');
