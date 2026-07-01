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
    onClientHello:     { layer: 'tls',  wired: true  },   // phase 3: server.ssl.onClientHello
    onClientHandshake: { layer: 'tls',  wired: false },
    onRequestHeaders:  { layer: 'http', wired: true  },
    onRequestBody:     { layer: 'http', wired: false },
    onSelectUpstream:  { layer: 'lb',   wired: false },   // gap: per-request LB
    onServerConnect:   { layer: 'lb',   wired: false },
    onResponseHeaders: { layer: 'http', wired: true  },
    onResponseBody:    { layer: 'http', wired: false },
    onClientClose:     { layer: 'l4',   wired: true  }    // phase 2: conn.onClose
};

// ---- capability table: which commands are valid in which event --------------
// This IS the pilgrim describe()/safety-class idea, indexed by EVENT/phase. A
// command used in the wrong event throws — the machine-checked "iRules command
// X is only valid in event Y" contract, made explicit.
var CAPS = {
    onClientHello:     ['flow', 'clientHello', 'table'],
    onClientAccept:    ['clientAddr', 'clientPort', 'flow', 'table', 'reject'],
    onRequestHeaders:  ['clientAddr', 'clientPort', 'flow', 'ctx', 'table',
                        'method', 'uri', 'header', 'respond', 'redirect',
                        'selectUpstream'],
    onResponseHeaders: ['clientAddr', 'clientPort', 'flow', 'ctx', 'table',
                        'setResponseHeader'],
    onClientClose:     ['flow', 'table']   // no request/conn at close; flow only
};

// ---- global store (iRules `table`) ------------------------------------------
// Phase 1: per-worker Map. Multi-worker binding = nginx.shared / SharedWorker;
// external binding = the db-connect project. TTL is a phase-2 concern.
var TABLE = new Map();

// ---- connection flow-local (iRules connection-local vars) -------------------
// PHASE 2: backed by pilgrim's REAL per-connection ctx object — conn.ctx in the
// accept hook, r.connCtx in request/response. It lives on the nginx connection
// pool, so it persists across every keepalive request and is freed when the
// connection closes. This replaces phase 1's 4-tuple-keyed Map approximation
// and its leak workaround. The close event (onClientClose) is wired to the new
// conn.onClose(). (Both were the phase-1 pilgrim gaps; now closed in the module.)

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
    Object.defineProperty(ev, 'clientHello', {
        get: function () { cap(event, 'clientHello'); return o.clientHello; } });

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
    // per-request LB / pool selection (iRules `pool`). Sets the nginx variable
    // $mirror_upstream, which the location's `proxy_pass http://$mirror_upstream`
    // resolves to a named upstream at request time.
    ev.selectUpstream = function (name) {
        cap(event, 'selectUpstream');
        o.r.setVariable('mirror_upstream', String(name));
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

    // onClientHello — TLS ClientHello inspection (server.ssl.onClientHello);
    // flow-local set here (pre-handshake) carries all the way to the response.
    if (handlers.onClientHello) {
        server.ssl.onClientHello(function (ch, connCtx) {
            handlers.onClientHello(
                makeEvent('onClientHello', { flow: connCtx, clientHello: ch }));
        });
    }

    // accept hook backs both onClientAccept and (via conn.onClose) onClientClose
    if (handlers.onClientAccept || handlers.onClientClose) {
        server.on('accept', function (conn) {
            if (handlers.onClientAccept) {
                handlers.onClientAccept(
                    makeEvent('onClientAccept', { conn: conn, flow: conn.ctx }));
            }
            if (handlers.onClientClose) {
                conn.onClose(function (connCtx) {
                    handlers.onClientClose(
                        makeEvent('onClientClose', { flow: connCtx }));
                });
            }
        });
    }
    if (handlers.onRequestHeaders) {
        location.addHook(function (r) {
            handlers.onRequestHeaders(
                makeEvent('onRequestHeaders', { r: r, flow: r.connCtx }));
        });
    }
    if (handlers.onResponseHeaders) {
        location.addResponseHook(function (r) {
            handlers.onResponseHeaders(
                makeEvent('onResponseHeaders', { r: r, flow: r.connCtx }));
        });
    }
}

globalThis.mirror = {
    version: '0.1.0-phase1',
    events:  EVENTS,
    caps:    CAPS,
    attach:  attach
};
