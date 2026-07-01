// Standalone acceptance test for the TCL/iRules -> mirror transpiler.
// Run via run.sh, which concatenates ../lib/transpile.js ahead of this file and
// executes both under qjs. No nginx required. Any failure throws (nonzero exit).

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

print('\nResults: ' + PASS + ' passed, ' + FAIL + ' failed');
if (FAIL > 0) { throw new Error(FAIL + ' transpiler test(s) failed'); }
