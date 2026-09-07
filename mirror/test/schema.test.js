// Drift test for the mirror typed API schema (M2).
//
// The schema is only worth having if it cannot silently disagree with reality.
// Three-way check, per event:
//
//   schema  <->  CAPS            (lib/mirror.js allow-list)
//   schema  <->  implementation  (EVENT_PROTO, via a live ev)
//
// This is the check that would have caught the `redirect` bug: `redirect` was
// in CAPS and emitted by the transpiler, but EVENT_PROTO.redirect did not
// exist, so every transpiled HTTP::redirect threw a TypeError at request time.
//
// Same shape/spirit as t/js_com_describe.t's drift invariant for the C-side
// describe() registry (every settable() name must have a describe() entry).
import * as std from "std";

globalThis.nginx = { log: function () {} };

(0, eval)(std.loadFile(scriptArgs[1]));      // mirror.js  -> globalThis.mirror
(0, eval)(std.loadFile(scriptArgs[2]));      // schema.js  -> mirror.schema

var PASS = 0, FAIL = 0;
function ok(desc, cond) {
    if (cond) { print('PASS: ' + desc); PASS++; }
    else      { print('FAIL: ' + desc); FAIL++; }
}

var M = globalThis.mirror;
var S = M && M.schema;
ok('schema is published on mirror', !!S && typeof S.capabilitiesOf === 'function');
ok('schema declares a version',     typeof S.schemaVersion === 'number' && S.schemaVersion >= 1);

// ---- obtain a live ev for the event under test ------------------------------
function liveEvent(event) {
    var captured = null;
    var location = {
        addHook:         function (fn) { captured = fn; },
        addResponseHook: function (fn) { captured = fn; }
    };
    var server = { on: function () {}, ssl: { onClientHello: function () {} } };
    var handlers = {};
    handlers[event] = function (ev) { captured = ev; };
    M.attach(server, location, handlers);
    var hook = captured;                       // the registered hook fn
    var r = {
        headers: {}, method: 'GET', uri: '/', ctx: {}, connCtx: {},
        respond: function () {}, setHeader: function () {},
        setVariable: function () {}, variable: function () { return ''; }
    };
    hook(r);                                   // handler stores ev into captured
    return captured;
}

var EVENTS_UNDER_TEST = Object.keys(S.events);
ok('schema covers onRequestHeaders', EVENTS_UNDER_TEST.indexOf('onRequestHeaders') >= 0);

EVENTS_UNDER_TEST.forEach(function (event) {
    var members = S.events[event];
    var caps    = (M.caps[event] || []).slice().sort();
    var declared = S.capabilitiesOf(event);

    // (1) schema <-> CAPS, both directions.
    var missingFromSchema = caps.filter(function (c) { return declared.indexOf(c) < 0; });
    var extraInSchema     = declared.filter(function (c) { return caps.indexOf(c) < 0; });
    ok(event + ': every CAPS capability has a schema signature — missing: [' +
       missingFromSchema.join(', ') + ']', missingFromSchema.length === 0);
    ok(event + ': schema declares no capability absent from CAPS — extra: [' +
       extraInSchema.join(', ') + ']', extraInSchema.length === 0);

    // (2) schema <-> implementation: every declared member must actually exist,
    //     with the right shape and (for methods) the right required arity.
    var ev = liveEvent(event);
    Object.keys(members).forEach(function (name) {
        var sig = members[name];
        var present, shapeOk = true, arityOk = true, detail = '';

        if (sig.kind === 'method') {
            present = typeof ev[name] === 'function';
            if (present) {
                // fn.length counts parameters BEFORE the first one with a
                // default value. mirror expresses optional params with `||`
                // defaults rather than default syntax, so .length is the TOTAL
                // declared count. Assert the range instead of pinning either
                // end, so the check survives that style choice while still
                // catching a parameter genuinely added or removed.
                var lo = S.requiredArity(sig), hi = sig.params.length, got = ev[name].length;
                arityOk = (got >= lo && got <= hi);
                if (!arityOk) {
                    detail = ' (schema says ' + lo + '..' + hi + ' params, impl .length=' + got + ')';
                }
            }
        } else if (sig.kind === 'namespace') {
            present = ev[name] !== undefined && ev[name] !== null;
            if (present) {
                var missing = Object.keys(sig.members).filter(function (m) {
                    return typeof ev[name][m] !== 'function';
                });
                shapeOk = missing.length === 0;
                if (!shapeOk) { detail = ' (namespace missing: ' + missing.join(', ') + ')'; }
            }
        } else {                                   // getter
            present = true;
            try { ev[name]; } catch (e) { present = false; detail = ' (' + e.message + ')'; }
        }

        ok(event + '.' + name + ': implemented' + detail, present && shapeOk && arityOk);
    });
});

// ---- lowerability report (informational, not a failure) ---------------------
// `any` marks a member the typed path cannot lower (falls back to boxed /
// interpreted). Keeping it visible is the point: it should shrink over time.
var anyMembers = [];
EVENTS_UNDER_TEST.forEach(function (event) {
    var m = S.events[event];
    Object.keys(m).forEach(function (name) {
        if (String(m[name].returns).indexOf('any') === 0) { anyMembers.push(event + '.' + name); }
    });
});
print('\nlowerability: ' + anyMembers.length + ' member(s) return `any` (boxed fallback): ' +
      anyMembers.join(', '));

print('\nResults: ' + PASS + ' passed, ' + FAIL + ' failed');
if (FAIL > 0) { std.exit(1); }
