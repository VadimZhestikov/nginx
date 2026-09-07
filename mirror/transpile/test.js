// Standalone acceptance test for the TCL/iRules -> mirror transpiler.
// Run via run.sh (qjs). It loads the transpiler and the showcase iRule from the
// paths passed as scriptArgs, so there is a single canonical copy of each.
// No nginx required. Any failure throws (nonzero exit).
import * as std from "std";

// M2c: the transpiler asks the typed schema whether a command is legal in the
// event being transpiled, so the test loads the real mirror + schema rather
// than letting it fall back. mirror.js touches `nginx` only lazily, so a
// minimal stub is enough.
globalThis.nginx = { log: function () {} };

(0, eval)(std.loadFile(scriptArgs[1]));      // mirror.js    -> globalThis.mirror
(0, eval)(std.loadFile(scriptArgs[2]));      // schema.js    -> mirror.schema
(0, eval)(std.loadFile(scriptArgs[3]));      // transpile.js -> globalThis.mirrorTranspile
var SHOWCASE_TCL = std.loadFile(scriptArgs[4]);

var PASS = 0, FAIL = 0;
function ok(desc, cond) {
    if (cond) { print('PASS: ' + desc); PASS++; }
    else      { print('FAIL: ' + desc); FAIL++; }
}
function has(hay, needle) { return String(hay).indexOf(needle) >= 0; }

var T = globalThis.mirrorTranspile;
ok('transpiler is loaded', typeof T === 'function');

// ---- fixture 1: the connection-lifecycle spine iRule ------------------------
// This is the SAME iRule that example/app.js translates by hand — the transpiler
// must reach the same mirror calls.
var spine = [
    'when CLIENT_ACCEPTED {',
    '    set client [IP::client_addr]',
    '    set n 0',
    '}',
    'when HTTP_REQUEST {',
    '    incr n',
    '    table incr mirror:total',
    '    set route [expr {[HTTP::header X-Mirror-Route] eq "beta" ? "beta" : "stable"}]',
    '}',
    'when HTTP_RESPONSE {',
    '    HTTP::header insert X-Mirror-Route $route',
    '    HTTP::header insert X-Mirror-Client $client',
    '}'
].join('\n');

var r1 = T(spine);
print('\n--- fixture 1 handlers ---\n' + r1.handlers + '\n');
ok('spine: maps all three events',
   r1.events.length === 3 &&
   has(r1.events.join(','), 'onClientAccept') &&
   has(r1.events.join(','), 'onRequestHeaders') &&
   has(r1.events.join(','), 'onResponseHeaders'));
ok('spine: no warnings', r1.warnings.length === 0);
ok('spine: not a stream rule', r1.isStream === false);
ok('spine: IP::client_addr -> ev.clientAddr', has(r1.handlers, 'ev.flow.client = ev.clientAddr;'));
ok('spine: set n 0 -> flow-local',            has(r1.handlers, 'ev.flow.n = 0;'));
ok('spine: incr -> flow-local +1',            has(r1.handlers, 'ev.flow.n = (ev.flow.n || 0) + 1;'));
ok('spine: table incr -> ev.table.incr',      has(r1.handlers, "ev.table.incr(\"mirror:total\");"));
ok('spine: expr ternary + HTTP::header eq',
   has(r1.handlers, 'ev.header("X-Mirror-Route") === "beta" ? "beta" : "stable"'));
ok('spine: HTTP::header insert -> setResponseHeader + $var',
   has(r1.handlers, 'ev.setResponseHeader("X-Mirror-Route", ev.flow.route);'));

// ---- behavioral: eval the generated handlers and drive them with a mock ev --
var nginxLog = [];
globalThis.nginx = { log: function (lvl, msg) { nginxLog.push(msg); } };

function mockEv(headers) {
    var flow = {}, table = {};
    return {
        flow: flow, _table: table,
        clientAddr: '1.2.3.4',
        header: function (k) { return headers[k]; },
        table: {
            incr: function (k) { table[k] = (table[k] || 0) + 1; return table[k]; },
            set:  function (k, v) { table[k] = v; return v; },
            get:  function (k) { return table[k]; }
        },
        respHeaders: {},
        setResponseHeader: function (k, v) { this.respHeaders[k] = v; },
        selectUpstream: function (n) { this.upstream = n; },
        reject: function () { this.rejected = true; }
    };
}

var H1 = (0, eval)('(' + r1.handlers + ')');
var accept = mockEv({});
H1.onClientAccept(accept);
ok('behavioral: accept stashes client in flow', accept.flow.client === '1.2.3.4');
ok('behavioral: accept sets n=0',               accept.flow.n === 0);

var req = mockEv({ 'X-Mirror-Route': 'beta' });
req.flow = accept.flow;                 // same connection -> same flow-local
H1.onRequestHeaders(req);
ok('behavioral: incr bumped flow.n to 1', req.flow.n === 1);
ok('behavioral: table:total incremented',  req._table['mirror:total'] === 1);
ok('behavioral: route resolved to beta',    req.flow.route === 'beta');

var resp = mockEv({});
resp.flow = req.flow;
H1.onResponseHeaders(resp);
ok('behavioral: response carries route header',  resp.respHeaders['X-Mirror-Route'] === 'beta');
ok('behavioral: response carries client header', resp.respHeaders['X-Mirror-Client'] === '1.2.3.4');

var reqStable = mockEv({ 'X-Mirror-Route': 'gamma' });
H1.onRequestHeaders(reqStable);
ok('behavioral: non-beta route -> stable', reqStable.flow.route === 'stable');

// ---- fixture 2: pool selection + TTL'd table set ----------------------------
var lb = [
    'when HTTP_REQUEST {',
    '    pool mirror_poolA',
    '    table set greeting "hi there" 30',
    '}'
].join('\n');
var r2 = T(lb);
print('\n--- fixture 2 handlers ---\n' + r2.handlers + '\n');
ok('lb: pool -> selectUpstream', has(r2.handlers, 'ev.selectUpstream("mirror_poolA");'));
ok('lb: table set with ttl',     has(r2.handlers, 'ev.table.set("greeting", "hi there", 30);'));
ok('lb: no warnings',            r2.warnings.length === 0);
var H2 = (0, eval)('(' + r2.handlers + ')');
var lbEv = mockEv({});
H2.onRequestHeaders(lbEv);
ok('behavioral: pool selected', lbEv.upstream === 'mirror_poolA');
ok('behavioral: table set',     lbEv._table.greeting === 'hi there');

// ---- fixture 3: L4 (CLIENT_DATA) -> stream rule -----------------------------
var l4 = [
    'when CLIENT_DATA {',
    '    log local0. "got L4 data"',
    '    reject',
    '}'
].join('\n');
var r3 = T(l4);
print('\n--- fixture 3 handlers ---\n' + r3.handlers + '\n');
ok('l4: maps onClientData',   r3.events.length === 1 && r3.events[0] === 'onClientData');
ok('l4: flagged as stream',   r3.isStream === true);
ok('l4: log -> nginx.log',    has(r3.handlers, 'nginx.log(5, "got L4 data");'));
ok('l4: reject -> ev.reject', has(r3.handlers, 'ev.reject();'));
var H3 = (0, eval)('(' + r3.handlers + ')');
var l4Ev = mockEv({});
H3.onClientData(l4Ev);
ok('behavioral: L4 logged',   nginxLog.indexOf('got L4 data') >= 0);
ok('behavioral: L4 rejected', l4Ev.rejected === true);

// ---- fixture 4: unsupported command must WARN, not silently drop ------------
var bad = [
    'when HTTP_REQUEST {',
    '    sideband connect foo',
    '}'
].join('\n');
var r4 = T(bad);
ok('unsupported: warning emitted', r4.warnings.length >= 1 &&
   has(r4.warnings.join('|'), "unsupported command 'sideband'"));
ok('unsupported: left as a comment (not dropped)', has(r4.handlers, '// unsupported: sideband'));

// ---- fixture 5: unknown event must WARN -------------------------------------
var r5 = T('when SERVER_CONNECTED { pool p }');
ok('unknown event: warning emitted', r5.warnings.length >= 1 &&
   has(r5.warnings.join('|'), "unsupported event 'SERVER_CONNECTED'"));

// ---- fixture 6: if / else control flow (phase 10) ---------------------------
var iff = [
    'when HTTP_REQUEST {',
    '    if { [HTTP::header X-Pool] eq "b" } {',
    '        pool mirror_poolB',
    '    } elseif { [HTTP::header X-Pool] eq "c" } {',
    '        pool mirror_poolC',
    '    } else {',
    '        pool mirror_poolA',
    '    }',
    '}'
].join('\n');
var r6 = T(iff);
print('\n--- fixture 6 handlers ---\n' + r6.handlers + '\n');
ok('if: no warnings',                r6.warnings.length === 0);
ok('if: emits if (...) {',           has(r6.handlers, 'if (ev.header("X-Pool") === "b") {'));
ok('if: emits } else if (...) {',    has(r6.handlers, '} else if (ev.header("X-Pool") === "c") {'));
ok('if: emits } else {',             has(r6.handlers, '} else {'));
var H6 = (0, eval)('(' + r6.handlers + ')');
var e6b = mockEv({ 'X-Pool': 'b' }); H6.onRequestHeaders(e6b);
var e6c = mockEv({ 'X-Pool': 'c' }); H6.onRequestHeaders(e6c);
var e6a = mockEv({ 'X-Pool': 'x' }); H6.onRequestHeaders(e6a);
ok('behavioral: if b -> poolB',    e6b.upstream === 'mirror_poolB');
ok('behavioral: elseif c -> poolC', e6c.upstream === 'mirror_poolC');
ok('behavioral: else -> poolA',    e6a.upstream === 'mirror_poolA');

// ---- fixture 7: switch control flow -----------------------------------------
var sw = [
    'when HTTP_REQUEST {',
    '    switch [HTTP::header X-Cmd] {',
    '        get  { table incr gets }',
    '        post { table incr posts }',
    '        default { table incr other }',
    '    }',
    '}'
].join('\n');
var r7 = T(sw);
print('\n--- fixture 7 handlers ---\n' + r7.handlers + '\n');
ok('switch: no warnings',        r7.warnings.length === 0);
ok('switch: emits switch (...)', has(r7.handlers, 'switch (ev.header("X-Cmd")) {'));
ok('switch: case "get"',         has(r7.handlers, 'case "get": {'));
ok('switch: default clause',     has(r7.handlers, 'default: {'));
var H7 = (0, eval)('(' + r7.handlers + ')');
var e7 = mockEv({ 'X-Cmd': 'post' }); H7.onRequestHeaders(e7);
var e7d = mockEv({ 'X-Cmd': 'zzz' }); H7.onRequestHeaders(e7d);
ok('behavioral: switch post -> posts', e7._table.posts === 1 && e7._table.gets === undefined);
ok('behavioral: switch default -> other', e7d._table.other === 1);

// ---- fixture 8: foreach over a literal list ---------------------------------
var fe = [
    'when HTTP_REQUEST {',
    '    foreach h {a b c} {',
    '        table incr $h',
    '    }',
    '}'
].join('\n');
var r8 = T(fe);
print('\n--- fixture 8 handlers ---\n' + r8.handlers + '\n');
ok('foreach: no warnings',           r8.warnings.length === 0);
ok('foreach: emits forEach literal', has(r8.handlers, '["a", "b", "c"].forEach(function (_it) {'));
var H8 = (0, eval)('(' + r8.handlers + ')');
var e8 = mockEv({}); H8.onRequestHeaders(e8);
ok('behavioral: foreach incremented all keys',
   e8._table.a === 1 && e8._table.b === 1 && e8._table.c === 1);

// ---- fixture 9: nested if inside switch (recursion) -------------------------
var nested = [
    'when HTTP_REQUEST {',
    '    switch [HTTP::header X-Cmd] {',
    '        get {',
    '            if { [HTTP::header X-Admin] eq "1" } { pool admin } else { pool users }',
    '        }',
    '        default { pool users }',
    '    }',
    '}'
].join('\n');
var r9 = T(nested);
ok('nested: no warnings', r9.warnings.length === 0);
var H9 = (0, eval)('(' + r9.handlers + ')');
var e9 = mockEv({ 'X-Cmd': 'get', 'X-Admin': '1' }); H9.onRequestHeaders(e9);
ok('behavioral: nested if-in-switch -> admin', e9.upstream === 'admin');

// ---- fixture 10: expr string operators + string/HTTP command subs (phase 14)
// give the mock ev uri/host/cookie for behavioural checks
function mockEv2(headers, uri, cookies) {
    var ev = mockEv(headers || {});
    ev.uri = uri || '/';
    ev.cookie = function (n) { return (cookies || {})[n]; };
    return ev;
}

var strr = [
    'when HTTP_REQUEST {',
    '    set host [string tolower [HTTP::host]]',
    '    if { [HTTP::path] starts_with "/api/" } { set area api } else { set area web }',
    '    if { [HTTP::header User-Agent] contains "bot" } { set bot 1 } else { set bot 0 }',
    '    set sid [HTTP::cookie session]',
    '}'
].join('\n');
var r10 = T(strr);
print('\n--- fixture 10 handlers ---\n' + r10.handlers + '\n');
ok('str: no warnings',              r10.warnings.length === 0);
ok('str: string tolower + HTTP::host',
   has(r10.handlers, 'String(ev.header("host")).toLowerCase()'));
ok('str: HTTP::path starts_with -> startsWith',
   has(r10.handlers, 'String(ev.uri).startsWith("/api/")'));
ok('str: header contains -> includes',
   has(r10.handlers, 'String(ev.header("User-Agent")).includes("bot")'));
ok('str: HTTP::cookie -> ev.cookie',  has(r10.handlers, 'ev.cookie("session")'));

var H10 = (0, eval)('(' + r10.handlers + ')');
var e10a = mockEv2({ 'host': 'API.Example.COM', 'User-Agent': 'good-bot/1' },
                   '/api/users', { session: 'abc' });
H10.onRequestHeaders(e10a);
ok('behavioral: host lowercased',   e10a.flow.host === 'api.example.com');
ok('behavioral: path starts_with -> api', e10a.flow.area === 'api');
ok('behavioral: UA contains bot -> 1',    e10a.flow.bot === 1);
ok('behavioral: cookie read',       e10a.flow.sid === 'abc');

var e10b = mockEv2({ 'host': 'x', 'User-Agent': 'human' }, '/home', {});
H10.onRequestHeaders(e10b);
ok('behavioral: non-api path -> web', e10b.flow.area === 'web');
ok('behavioral: no bot -> 0',         e10b.flow.bot === 0);

// ---- fixture 11: ends_with + equals + substr + string length ----------------
var strr2 = [
    'when HTTP_REQUEST {',
    '    if { [HTTP::path] ends_with ".json" } { set fmt json } else { set fmt html }',
    '    if { [HTTP::method] equals "POST" } { set write 1 } else { set write 0 }',
    '    set head [substr [HTTP::path] 0 4]',
    '    set len [string length [HTTP::path]]',
    '}'
].join('\n');
var r11 = T(strr2);
ok('str2: no warnings',          r11.warnings.length === 0);
ok('str2: ends_with -> endsWith', has(r11.handlers, 'String(ev.uri).endsWith(".json")'));
ok('str2: equals -> ===',         has(r11.handlers, 'ev.method === "POST"'));
ok('str2: substr',                has(r11.handlers, 'String(ev.uri).substr(0, 4)'));
ok('str2: string length',         has(r11.handlers, 'String(ev.uri).length'));
var H11 = (0, eval)('(' + r11.handlers + ')');
var e11 = mockEv2({}, '/data.json', {}); e11.method = 'POST';
H11.onRequestHeaders(e11);
ok('behavioral: ends_with .json -> json', e11.flow.fmt === 'json');
ok('behavioral: method equals POST -> 1', e11.flow.write === 1);
ok('behavioral: substr head',             e11.flow.head === '/dat');
ok('behavioral: length',                  e11.flow.len === 10);

// ---- fixture 12: unknown expr word must WARN (not silently mistranslate) ----
var r12 = T('when HTTP_REQUEST { set x [expr {$a wibble $b}] }');
ok('expr: unknown word warns', r12.warnings.length >= 1 &&
   has(r12.warnings.join('|'), "unknown expr word 'wibble'"));

// ---- fixture 13: data groups — class match / class lookup (phase 15) --------
// generated code calls mirror.classMatch/classLookup; provide a tiny stub with
// the real semantics for the behavioural check.
var DG = { blocklist: ['1.2.3.4', '9.9.9.9'], routes: { api: 'poolB', web: 'poolA' } };
function dgCmp(s, op, e) {
    s = String(s); e = String(e);
    if (op === 'contains') { return s.indexOf(e) >= 0; }
    return s === e;
}
// AUGMENT the loaded mirror, do not replace it: the transpiler now asks
// mirror.schema whether a command is legal in an event (M2c), so clobbering
// globalThis.mirror here would strip caps/schema and silently make every
// capability look permitted.
globalThis.mirror = globalThis.mirror || {};
globalThis.mirror.classMatch = function (name, op, subj) {
    var g = DG[name]; if (!g) { return false; }
    var items = Array.isArray(g) ? g : Object.keys(g);
    for (var i = 0; i < items.length; i++) { if (dgCmp(subj, op, items[i])) { return true; } }
    return false;
};
globalThis.mirror.classLookup = function (name, key) {
    var g = DG[name]; if (!g) { return undefined; }
    return Array.isArray(g) ? (g.indexOf(key) >= 0 ? key : undefined) : g[key];
};

var dgRule = [
    'when HTTP_REQUEST {',
    '    if { [class match [IP::client_addr] equals blocklist] } { set blk 1 } else { set blk 0 }',
    '    set pool [class lookup [HTTP::header X-Area] routes]',
    '}'
].join('\n');
var r13 = T(dgRule);
print('\n--- fixture 13 handlers ---\n' + r13.handlers + '\n');
ok('class: no warnings',             r13.warnings.length === 0);
ok('class match -> mirror.classMatch',
   has(r13.handlers, 'mirror.classMatch("blocklist", "equals", ev.clientAddr)'));
ok('class lookup -> mirror.classLookup',
   has(r13.handlers, 'mirror.classLookup("routes", ev.header("X-Area"))'));
var H13 = (0, eval)('(' + r13.handlers + ')');
var e13blk = mockEv({}); e13blk.clientAddr = '9.9.9.9';
H13.onRequestHeaders(e13blk);
ok('behavioral: blocked IP matches datagroup', e13blk.flow.blk === 1);
var e13ok = mockEv({ 'X-Area': 'api' }); e13ok.clientAddr = '8.8.8.8';
H13.onRequestHeaders(e13ok);
ok('behavioral: allowed IP -> 0',       e13ok.flow.blk === 0);
ok('behavioral: class lookup -> value', e13ok.flow.pool === 'poolB');

// ---- fixture 14: the CAPSTONE — a realistic, production-shaped iRule --------
// showcase.tcl exercises the whole command surface (events, data groups, string
// ops, if/elseif/else, switch, early return, table, pool, response headers) in
// one rule. Transpiling it with ZERO warnings is the strongest single signal
// that the mirror model covers the iRules surface; then we drive every branch.
DG.ip_blocklist = ['10.0.0.5', '203.0.113.9'];
DG.bad_agents   = ['badbot', 'evilscanner'];
DG.routes       = { api: 'poolB', static: 'poolA', web: 'poolA' };

var rc = T(SHOWCASE_TCL);
print('\n--- showcase handlers ---\n' + rc.handlers + '\n');
if (rc.warnings.length) { print('showcase warnings: ' + rc.warnings.join(' | ')); }
ok('showcase: transpiles with ZERO warnings', rc.warnings.length === 0);
ok('showcase: maps all four events',
   rc.events.length === 4 &&
   has(rc.events.join(','), 'onClientAccept') &&
   has(rc.events.join(','), 'onRequestHeaders') &&
   has(rc.events.join(','), 'onResponseHeaders') &&
   has(rc.events.join(','), 'onClientClose'));
ok('showcase: early return after respond', has(rc.handlers, 'return;'));

// drive every branch behaviourally
function scMockEv(headers, uri) {
    var ev = mockEv(headers || {});
    ev.uri = uri || '/';
    ev.responded = null;
    ev.respond = function (code, h, body) { ev.responded = { code: code, body: body }; ev.stopped = true; };
    return ev;
}
var H14 = (0, eval)('(' + rc.handlers + ')');

// (a) accept stashes client IP + bumps conn counter
var acc = scMockEv({}); acc.clientAddr = '8.8.8.8';
H14.onClientAccept(acc);
ok('showcase: accept stashes cip',    acc.flow.cip === '8.8.8.8');
ok('showcase: accept counts conn',    acc._table['stats:conns'] === 1);

// (b) blocklisted XFF -> 403 + early return (area never set)
var blk = scMockEv({ 'X-Forwarded-For': '10.0.0.5' }, '/api/x');
H14.onRequestHeaders(blk);
ok('showcase: blocked XFF -> 403',        blk.responded && blk.responded.code === 403);
ok('showcase: block increments counter',  blk._table['stats:blocked'] === 1);
ok('showcase: early return (no routing)', blk.flow.area === undefined && blk.upstream === undefined);

// (c) clean /api/ request -> api area, poolB, bot flag, canary channel
var api = scMockEv({ 'User-Agent': 'Evil-BadBot/2', 'X-Channel': 'canary' }, '/API/users');
H14.onRequestHeaders(api);
ok('showcase: path tolower + starts_with -> api', api.flow.area === 'api');
ok('showcase: class lookup routes -> poolB',      api.upstream === 'poolB');
ok('showcase: UA contains bad_agents -> flagged',  api.flow.flagged === 1);
ok('showcase: switch X-Channel -> canary',         api.flow.channel === 'canary');

// (d) plain web request -> web area, poolA, default channel, not flagged
var web = scMockEv({ 'User-Agent': 'Mozilla', 'X-Channel': 'zzz' }, '/home');
H14.onRequestHeaders(web);
ok('showcase: default path -> web/poolA', web.flow.area === 'web' && web.upstream === 'poolA');
ok('showcase: switch default -> stable',  web.flow.channel === 'stable');
ok('showcase: clean UA -> not flagged',   web.flow.flagged === 0);

// (e) response inserts the security + context headers
var resp = scMockEv({}); resp.flow = web.flow;
H14.onResponseHeaders(resp);
ok('showcase: response sets X-Area',       resp.respHeaders['X-Area'] === 'web');
ok('showcase: response sets X-Frame-Options', resp.respHeaders['X-Frame-Options'] === 'DENY');
ok('showcase: response sets HSTS',
   resp.respHeaders['Strict-Transport-Security'] === 'max-age=31536000');

// ---- `reject` is event-scoped ----------------------------------------------
// Regression: `reject`/`TCP::close` used to emit ev.reject() in EVERY event.
// mirror only exposes reject where the event carries a connection (the L4
// accept/data events); in an HTTP event the emitted call threw at request time
// ("command 'reject' is not valid in event 'onRequestHeaders'"). The transpiler
// must now warn and drop it instead of emitting a guaranteed runtime failure.
var rj1 = T('when HTTP_REQUEST { reject }');
ok('reject in HTTP event: not emitted',   !has(rj1.handlers, 'ev.reject()'));
ok('reject in HTTP event: warned',        rj1.warnings.length >= 1 &&
   has(rj1.warnings.join('|'), 'not available in onRequestHeaders'));

var rj2 = T('when CLIENT_ACCEPTED { reject }');
ok('reject in L4 accept: still emitted',  has(rj2.handlers, 'ev.reject()'));
ok('reject in L4 accept: no warning',     rj2.warnings.length === 0);

var rj3 = T('when CLIENT_DATA { TCP::close }');
ok('TCP::close in L4 data: still emitted', has(rj3.handlers, 'ev.reject()'));

// a rule that is otherwise valid must still transpile around a dropped reject
var rj4 = T('when HTTP_REQUEST { table incr hits\n reject }');
ok('reject dropped but rest of rule survives', has(rj4.handlers, 'ev.table.incr('));

print('\nResults: ' + PASS + ' passed, ' + FAIL + ' failed');
if (FAIL > 0) { throw new Error(FAIL + ' transpiler test(s) failed'); }
