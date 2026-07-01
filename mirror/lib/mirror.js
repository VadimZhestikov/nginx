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
    onClientData:      { layer: 'l4',   wired: true  },   // phase 6: stream preread
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
    onClientClose:     ['flow', 'table'],  // no request/conn at close; flow only
    onClientData:      ['data', 'clientAddr', 'table', 'finalize', 'reject']  // L4 (stream)
};

// ---- global store (iRules `table`) — cross-worker in phase 7 ----------------
// The iRules `table` is cluster-shared state; the nginx analog is cross-WORKER
// state. Phase 1 used a per-worker Map, so a counter incremented on worker A was
// invisible to worker B — wrong for a shared table. Phase 7 backs `table` with
// pilgrim's cross-worker `nginx.shared` (a shared-memory KV store): values are
// JSON-encoded, and `incr` maps to `nginx.shared.incr`, which is ATOMIC under a
// spinlock (no read-modify-write race across workers).
//
// The backend is probed LAZILY, not at load: `nginx.shared` throws at
// config-eval time (js_source runs before the zone exists). On the first table
// op inside a worker the probe succeeds and we bind to shared; if it ever throws
// (config time, or a build without the zone) we fall back to a per-worker Map so
// mirror stays usable.
//
// PHASE 8: entries can carry a TTL (iRules `table set key val <timeout>`).
// table.set(k, v, ttlSeconds) expires the entry after ttlSeconds; the shared
// backend enforces it in the module (lazy reclaim on access, atomic under the
// zone spinlock). table.ttl(k) reports the remaining lifetime. External bindings
// (db-connect / persistence COM) remain a later tier.
var TABLE_MAP = new Map();      // per-worker fallback: k -> {v, exp} (exp: ms, 0=never)
var _backend  = null;           // 'shared' | 'map' | null (unresolved)

function tableBackend() {
    if (_backend === null) {
        try {
            nginx.shared.get('__mirror_probe__');   // throws at config time
            _backend = 'shared';
        } catch (e) {
            _backend = 'map';
        }
    }
    return _backend;
}

// per-worker Map fallback, TTL-aware (used only when 'shared' is unavailable)
function mapGet(k) {
    var e = TABLE_MAP.get(k);
    if (e === undefined) { return undefined; }
    if (e.exp !== 0 && Date.now() >= e.exp) { TABLE_MAP.delete(k); return undefined; }
    return e.v;
}

// The single shared table object (cross-worker when backend === 'shared').
var TABLE = {
    get: function (k) {
        if (tableBackend() === 'shared') {
            var s = nginx.shared.get(k);
            if (s === undefined) { return undefined; }
            try { return JSON.parse(s); } catch (e) { return s; }
        }
        return mapGet(k);
    },
    // set(key, value[, ttlSeconds]) — ttlSeconds > 0 expires the entry.
    set: function (k, v, ttl) {
        if (tableBackend() === 'shared') {
            nginx.shared.set(k, JSON.stringify(v), ttl);
            return v;
        }
        var exp = (ttl > 0) ? Date.now() + ttl * 1000 : 0;
        TABLE_MAP.set(k, { v: v, exp: exp });
        return v;
    },
    incr: function (k, d) {
        d = (d === undefined) ? 1 : d;
        if (tableBackend() === 'shared') { return nginx.shared.incr(k, d); }
        var v = (mapGet(k) || 0) + d; TABLE_MAP.set(k, { v: v, exp: 0 }); return v;
    },
    delete: function (k) {
        if (tableBackend() === 'shared') { return nginx.shared.delete(k); }
        return TABLE_MAP.delete(k);
    },
    keys: function () {
        if (tableBackend() === 'shared') { return nginx.shared.keys(); }
        var out = [];
        TABLE_MAP.forEach(function (e, k) { if (mapGet(k) !== undefined) { out.push(k); } });
        return out;
    },
    // ttl(key): null = absent/expired, -1 = permanent, >=0 = seconds remaining.
    ttl: function (k) {
        if (tableBackend() === 'shared') { return nginx.shared.ttl(k); }
        var e = TABLE_MAP.get(k);
        if (e === undefined) { return null; }
        if (e.exp === 0) { return -1; }
        var rem = e.exp - Date.now();
        if (rem <= 0) { TABLE_MAP.delete(k); return null; }
        return Math.floor(rem / 1000);
    },
    backend: function () { return tableBackend(); }
};

// Capability-gated table facade for an event: gates the 'table' capability, then
// delegates to the single shared TABLE. Shared by makeEvent + makeStreamEvent.
function makeTableFacade(event) {
    return {
        get:    function (k)      { cap(event, 'table'); return TABLE.get(k); },
        set:    function (k, v, t){ cap(event, 'table'); return TABLE.set(k, v, t); },
        incr:   function (k, d)   { cap(event, 'table'); return TABLE.incr(k, d); },
        delete: function (k)      { cap(event, 'table'); return TABLE.delete(k); },
        keys:   function ()       { cap(event, 'table'); return TABLE.keys(); },
        ttl:    function (k)      { cap(event, 'table'); return TABLE.ttl(k); }
    };
}

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

    ev.table = makeTableFacade(event);

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

// ---- L4 / stream: onClientData (iRules CLIENT_DATA) -------------------------
// Raw TCP inspection. The stream session's preread bytes (session.data) are
// captured in the stream preread phase; the handler can route/reject on them.
function makeStreamEvent(event, session) {
    var ev = { event: event };
    Object.defineProperty(ev, 'data', {
        get: function () { cap(event, 'data'); return session.data; } });
    Object.defineProperty(ev, 'clientAddr', {
        get: function () { cap(event, 'clientAddr'); return session.remoteAddress; } });
    ev.table = makeTableFacade(event);
    ev.finalize = function (code) { cap(event, 'finalize'); session.finalize(code || 200); };
    ev.reject   = function ()     { cap(event, 'reject');   session.finalize(403); };
    return ev;
}

// Attach an L4 rule to a stream server: mirror.attachStream(streamServer, {onClientData})
function attachStream(streamServer, handlers) {
    Object.keys(handlers).forEach(function (k) {
        if (k !== 'onClientData') {
            nginx.log(5, "mirror.attachStream: only onClientData is supported (ignored: " + k + ")");
        }
    });
    if (handlers.onClientData) {
        // opt in to preread capture so session.data holds the client's first
        // bytes; without this the stream content handler is not delayed.
        streamServer.captureData = true;
        streamServer.handler = function (session) {
            handlers.onClientData(makeStreamEvent('onClientData', session));
        };
    }
}

// ---- live: transpile an iRule and attach it (phase 11) ----------------------
// Closes the transpiler loop: turn TCL/iRules source into a running mirror rule
// at config-eval time. mirror.transpile (from lib/transpile.js) emits the
// handlers as JS SOURCE; compileRule evals that into a real handlers object and
// applyRule attaches it to the right surface (HTTP server+location, or a stream
// server for L4 rules). Load order in nginx.conf: mirror.js, then transpile.js,
// then the app that calls applyRule.
function compileRule(tclSource) {
    if (!globalThis.mirror || typeof globalThis.mirror.transpile !== 'function') {
        throw new Error('mirror.compileRule: transpiler not loaded ' +
                        '(add `js_source lib/transpile.js`)');
    }
    var out = globalThis.mirror.transpile(tclSource);
    out.warnings.forEach(function (w) { nginx.log(5, 'mirror.transpile: ' + w); });
    // indirect eval -> the object literal is built in global scope; the handler
    // closures capture nothing but globals (ev is a param, nginx is global).
    out.handlersObj = (0, eval)('(' + out.handlers + ')');
    return out;
}

// applyRule(target, tclSource): target = {server, location} for an HTTP rule, or
// {streamServer} (or the stream server itself) for an L4 rule.
function applyRule(target, tclSource) {
    var out = compileRule(tclSource);
    if (out.isStream) {
        attachStream(target.streamServer || target, out.handlersObj);
    } else {
        attach(target.server, target.location, out.handlersObj);
    }
    return out;
}

globalThis.mirror = {
    version:      '0.1.0-phase11',
    events:       EVENTS,
    caps:         CAPS,
    attach:       attach,
    attachStream: attachStream,
    applyRule:    applyRule,      // transpile TCL/iRules + attach (live)
    compileRule:  compileRule,    // transpile TCL/iRules -> {handlersObj, ...}
    table:        TABLE          // cross-worker store (get/set/incr/delete/keys/backend)
};
