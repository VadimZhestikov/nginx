// mirror — unified event/command model for nginx (and, later, BIG-IP).
//
// PROJECT "mirror" (F5): converge nginx <-> BIG-IP. This file is phase 1 of the
// event/command model (threads 1 + 3): a clean, iRules-INSPIRED JS model
// (decision B — NOT a TCL port; a TCL->JS compat layer comes later as decision
// A) exposing the HTTP command surface on the FULL connection-lifecycle spine.
//
// It runs as a PURE JS LAYER over pilgrim hooks — no C changes in phase 1:
//   onClientAccept    -> server.on('accept', fn)         (NginxConnection)
//   onRequestHeaders  -> location.addHook(fn)            (request)
//   onResponseHeaders -> location.addResponseHook(fn)    (r.setHeader)
//
// Load before the app rules:  js_source .../mirror/lib/mirror.js;
// It publishes `globalThis.mirror`.  See ../README.md for the plan + findings.

// ---- canonical event lattice (FULL stack; only some wired in phase 1) --------
// Declaring the whole stack now keeps the model stack-shaped, not HTTP-shaped;
// L4/TLS/LB events get bindings in phase 2 (+ closing nginx API gaps).
var EVENTS = {
    onClientAccept:    { layer: 'l4',   wired: true  },
    onClientData:      { layer: 'l4',   wired: false },
    onClientHello:     { layer: 'tls',  wired: false },
    onClientHandshake: { layer: 'tls',  wired: false },
    onRequestHeaders:  { layer: 'http', wired: true  },
    onRequestBody:     { layer: 'http', wired: false },
    onSelectUpstream:  { layer: 'lb',   wired: false },   // gap: per-request LB
    onServerConnect:   { layer: 'lb',   wired: false },
    onResponseHeaders: { layer: 'http', wired: true  },
    onResponseBody:    { layer: 'http', wired: false },
    onClientClose:     { layer: 'l4',   wired: false }    // gap: no pilgrim close hook
};

// ---- capability table: which commands are valid in which event --------------
// This IS the pilgrim describe()/safety-class idea, indexed by EVENT/phase. A
// command used in the wrong event throws — the machine-checked "iRules command
// X is only valid in event Y" contract, made explicit.
var CAPS = {
    onClientAccept:    ['clientAddr', 'clientPort', 'flow', 'reject'],
    onRequestHeaders:  ['clientAddr', 'clientPort', 'flow', 'ctx', 'table',
                        'method', 'uri', 'header', 'respond', 'redirect'],
    onResponseHeaders: ['clientAddr', 'clientPort', 'flow', 'ctx', 'table',
                        'setResponseHeader']
};

// ---- global store (iRules `table`) ------------------------------------------
// Phase 1: per-worker Map. Multi-worker binding = nginx.shared / SharedWorker;
// external binding = the db-connect project. TTL is a phase-2 concern.
var TABLE = new Map();

// ---- connection flow-local store (iRules connection-local vars) -------------
// KEYED BY THE 4-TUPLE (remote addr:port) as an APPROXIMATION. pilgrim exposes
// no per-connection handle linking accept -> request, and no close hook to evict
// — that missing "per-connection ctx + close event" is the phase-1 FINDING and a
// phase-2 pilgrim gap (thread 1). accept + every request of a connection run in
// ONE worker, so a per-worker Map is correct; it is size-capped to bound the
// leak that the absent close hook would otherwise cause.
var FLOW = new Map();
var FLOW_CAP = 4096;
function flowKey(addr, port) { return addr + ':' + port; }
function flowGet(addr, port) {
    var k = flowKey(addr, port);
    var f = FLOW.get(k);
    if (!f) {
        if (FLOW.size >= FLOW_CAP) { FLOW.clear(); }   // crude bound; see finding
        f = {};
        FLOW.set(k, f);
    }
    return f;
}

// ---- capability-gated event context -----------------------------------------
function cap(event, name) {
    var allowed = CAPS[event] || [];
    if (allowed.indexOf(name) < 0) {
        throw new Error("mirror: command '" + name + "' is not valid in event '" +
                        event + "' (valid here: " + allowed.join(', ') + ")");
    }
}

function makeEvent(event, o) {   // o = { r?, conn?, flow }
    var ev = { event: event };

    Object.defineProperty(ev, 'flow', {
        get: function () { cap(event, 'flow'); return o.flow; } });
    Object.defineProperty(ev, 'ctx', {
        get: function () { cap(event, 'ctx'); return o.r.ctx; } });

    ev.table = {
        get:  function (k)    { cap(event, 'table'); return TABLE.get(k); },
        set:  function (k, v) { cap(event, 'table'); TABLE.set(k, v); return v; },
        incr: function (k, d) { cap(event, 'table');
                                var v = (TABLE.get(k) || 0) + (d === undefined ? 1 : d);
                                TABLE.set(k, v); return v; }
    };

    Object.defineProperty(ev, 'clientAddr', { get: function () {
        cap(event, 'clientAddr');
        return o.conn ? o.conn.remoteAddr : o.r.variable('remote_addr'); } });
    Object.defineProperty(ev, 'clientPort', { get: function () {
        cap(event, 'clientPort');
        return o.conn ? o.conn.remotePort : o.r.variable('remote_port'); } });
    Object.defineProperty(ev, 'method', { get: function () {
        cap(event, 'method'); return o.r.method; } });
    Object.defineProperty(ev, 'uri', { get: function () {
        cap(event, 'uri'); return o.r.uri; } });

    ev.header = function (name) {
        cap(event, 'header');
        var h = o.r.headers || {};
        return h[name] !== undefined ? h[name] : h[String(name).toLowerCase()];
    };

    // ---- flow-control / control verbs ----
    ev.respond = function (code, headers, body) {
        cap(event, 'respond');
        o.r.respond(code, headers || {}, body || '');
        ev.stopped = true;                       // short-circuit signal
    };
    ev.setResponseHeader = function (name, val) {
        cap(event, 'setResponseHeader');
        o.r.setHeader(name, String(val));
    };
    ev.reject = function () { cap(event, 'reject'); o.conn.reject(); };

    return ev;
}

// ---- attach a rule (set of per-event handlers) to a server + location -------
function attach(server, location, handlers) {
    Object.keys(handlers).forEach(function (k) {
        if (!EVENTS[k]) {
            throw new Error("mirror.attach: unknown event '" + k + "'");
        }
        if (!EVENTS[k].wired) {
            nginx.log(5, "mirror: event '" + k + "' not wired in phase 1 (ignored)");
        }
    });

    if (handlers.onClientAccept) {
        server.on('accept', function (conn) {
            var flow = flowGet(conn.remoteAddr, conn.remotePort);
            handlers.onClientAccept(makeEvent('onClientAccept', { conn: conn, flow: flow }));
        });
    }
    if (handlers.onRequestHeaders) {
        location.addHook(function (r) {
            var flow = flowGet(r.variable('remote_addr'), r.variable('remote_port'));
            handlers.onRequestHeaders(makeEvent('onRequestHeaders', { r: r, flow: flow }));
        });
    }
    if (handlers.onResponseHeaders) {
        location.addResponseHook(function (r) {
            var flow = flowGet(r.variable('remote_addr'), r.variable('remote_port'));
            handlers.onResponseHeaders(makeEvent('onResponseHeaders', { r: r, flow: flow }));
        });
    }
}

globalThis.mirror = {
    version: '0.1.0-phase1',
    events:  EVENTS,
    caps:    CAPS,
    attach:  attach
};
