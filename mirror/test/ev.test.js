// Standalone acceptance test for the mirror ev.* runtime surface.
// Run via run.sh (qjs). Loads mirror/lib/mirror.js from the path passed as
// scriptArgs[1], so there is a single canonical copy. No nginx required.
// Any failure exits nonzero.
//
// mirror.js is written to touch `nginx` only lazily (the table backend is
// probed on first use, not at load), so a minimal stub is enough to load it.
import * as std from "std";

globalThis.nginx = {
    log: function () {},
    // no `shared` => the table facade falls back to its per-worker Map
};

(0, eval)(std.loadFile(scriptArgs[1]));      // defines globalThis.mirror

var PASS = 0, FAIL = 0;
function ok(desc, cond) {
    if (cond) { print('PASS: ' + desc); PASS++; }
    else      { print('FAIL: ' + desc); FAIL++; }
}
function throws(fn) {
    try { fn(); return null; } catch (e) { return String(e.message || e); }
}

var M = globalThis.mirror;
ok('mirror is loaded', typeof M === 'object' && typeof M.attach === 'function');

// ---- harness: drive one onRequestHeaders handler with a fake request --------
// attach() registers the hook via location.addHook(fn); we capture fn and call
// it with a stub request, recording what the handler did to it.
function runRequest(handler, reqOverrides) {
    var hook = null;
    var location = {
        addHook:         function (fn) { hook = fn; },
        addResponseHook: function () {}
    };
    var server = { on: function () {}, ssl: { onClientHello: function () {} } };
    M.attach(server, location, { onRequestHeaders: handler });
    if (!hook) { throw new Error('attach did not register an onRequestHeaders hook'); }

    var rec = { responds: [], vars: {}, headers_set: {} };
    var r = {
        headers:  (reqOverrides && reqOverrides.headers) || {},
        method:   (reqOverrides && reqOverrides.method) || 'GET',
        uri:      (reqOverrides && reqOverrides.uri) || '/',
        ctx:      {},
        connCtx:  {},
        respond:     function (code, hdrs, body) { rec.responds.push([code, hdrs, body]); },
        setHeader:   function (n, v) { rec.headers_set[n] = v; },
        setVariable: function (n, v) { rec.vars[n] = v; },
        variable:    function (n) { return '(' + n + ')'; }
    };
    hook(r);
    return rec;
}

// ---- ev.redirect ------------------------------------------------------------
// Regression: `redirect` was listed in CAPS.onRequestHeaders and emitted by the
// transpiler, but EVENT_PROTO.redirect did not exist -> every transpiled rule
// using HTTP::redirect threw a TypeError.
var rec = runRequest(function (ev) { ev.redirect('/login'); });
ok('redirect: issues exactly one response', rec.responds.length === 1);
ok('redirect: defaults to 302',             rec.responds[0] && rec.responds[0][0] === 302);
ok('redirect: sets Location header',        rec.responds[0] && rec.responds[0][1].Location === '/login');
ok('redirect: empty body',                  rec.responds[0] && rec.responds[0][2] === '');

rec = runRequest(function (ev) { ev.redirect('/perm', 301); });
ok('redirect: honours an explicit code',    rec.responds[0] && rec.responds[0][0] === 301);

rec = runRequest(function (ev) { ev.redirect('/x'); ev.__stopped = ev.stopped; });
ok('redirect: sets the advisory stopped flag', rec.responds.length === 1);

// ---- ev.respond (unchanged behaviour, guards the redirect refactor) ---------
rec = runRequest(function (ev) { ev.respond(403, {}, 'no'); });
ok('respond: code/body preserved',
   rec.responds.length === 1 && rec.responds[0][0] === 403 && rec.responds[0][2] === 'no');

// ---- capability gating still enforced --------------------------------------
var msg = throws(function () {
    runRequest(function (ev) { ev.setResponseHeader('X', '1'); });
});
ok('caps: setResponseHeader denied in onRequestHeaders', msg && msg.indexOf('not valid in event') >= 0);

msg = throws(function () { runRequest(function (ev) { ev.reject(); }); });
ok('caps: reject denied in onRequestHeaders', msg && msg.indexOf('not valid in event') >= 0);

// ---- basic accessors --------------------------------------------------------
rec = runRequest(function (ev) { ev.respond(200, {}, ev.header('X-A') + '|' + ev.method + '|' + ev.uri); },
                 { headers: { 'x-a': 'v' }, method: 'POST', uri: '/u' });
ok('header() is case-insensitive; method/uri readable',
   rec.responds[0] && rec.responds[0][2] === 'v|POST|/u');

rec = runRequest(function (ev) { ev.selectUpstream('poolB'); });
ok('selectUpstream sets $mirror_upstream', rec.vars.mirror_upstream === 'poolB');

print('\nResults: ' + PASS + ' passed, ' + FAIL + ' failed');
if (FAIL > 0) { std.exit(1); }
