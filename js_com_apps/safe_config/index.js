// safe-config — Layer 2 policy plugin over the COM safety classes (Layer 1).
//
// Installs nginx.safeConfig: a safety GATEWAY that consults nginx.describe()
// before every mutation, enforces a per-tier opt-in, and routes the change by
// its propagation class (worker-local writes are fanned out to every worker;
// zoned-shared / auto-shared writes apply directly).
//
//   nginx.safeConfig.apply(path, value [, opts])
//       opts: { ack:true }     required for 'guarded' members
//             { confirm:true } required for 'irreversible' members
//             { dryRun:true }  return the plan without applying
//   nginx.safeConfig.plan(path, value)   // = apply(..., {dryRun:true})
//
//   Named policies (vetted vocabulary built on apply()):
//     canaryWeight(upstream, pct [, opts])
//     setResponseHeader(server, loc, key, value [, opts])
//     toggleLocation(server, loc, on [, opts])
//     drainPeer(upstream, addr [, opts])
//
// nginx.pluginConfig (second arg to nginx.use()):
//   { cfgWorkerPath: "<abs path>/cfgworker.js" }
//       enables cross-worker fan-out.  Without it, writes apply to the
//       calling worker only and plans report fannedOut:false.

(function () {

var cfg     = nginx.pluginConfig || {};
var _fanout = null;                 /* SharedWorker, or null if not configured */

/* SharedWorker is created in master (init_conf) so all workers inherit the
 * handle via COW; each worker registers onmessage + connects in broadcast(). */
if (cfg.cfgWorkerPath) {
    _fanout = new SharedWorker(cfg.cfgWorkerPath);
}

/* Handler installed on disabled locations by toggleLocation(..., false). */
function disabledHandler(r) {
    r.respond(503, {'Content-Type': 'text/plain'}, 'route disabled\n');
}

/* Per-worker stash of the handler a location had before it was disabled, so
 * re-enabling restores the exact prior handler (JS or original) rather than
 * unconditionally falling back to the original C handler via clearHandler(). */
var _priorHandler = {};   /* "serverName locPath" → function | undefined */

function toggleKey(serverName, locPath) {
    return serverName + ' ' + locPath;
}

function applyToggle(loc, serverName, locPath, on) {
    var key = toggleKey(serverName, locPath);
    if (on) {
        if (Object.prototype.hasOwnProperty.call(_priorHandler, key)
            && typeof _priorHandler[key] === 'function') {
            loc.handler = _priorHandler[key];     /* restore exact prior JS handler */
        } else {
            loc.clearHandler();                   /* restore the original C handler */
        }
        delete _priorHandler[key];
    } else {
        var cur = loc.handler;                    /* getter returns fn or undefined */
        _priorHandler[key] = (typeof cur === 'function') ? cur : undefined;
        loc.handler = disabledHandler;
    }
}

/* ------------------------------------------------------------------ *
 * Local command executor — runs in every worker via the fan-out path  *
 * ------------------------------------------------------------------ */
function execCmd(cmd) {
    if (cmd.kind === 'set') {
        nginx.set(cmd.path, cmd.value);

    } else if (cmd.kind === 'toggle') {
        var loc = resolveLoc(cmd.serverName, cmd.locPath);
        if (loc) { applyToggle(loc, cmd.serverName, cmd.locPath, cmd.on); }
    }
}

nginx.broadcast(function () {
    if (_fanout) {
        _fanout.onmessage = function (msg) {
            if (msg.data && msg.data.type === 'op') { execCmd(msg.data.cmd); }
        };
        /* Establish this worker's port so it receives fan-out. */
        _fanout.postMessage({ type: 'hello' });
    }
});

/* ------------------------------------------------------------------ *
 * Path / object resolution helpers                                    *
 * ------------------------------------------------------------------ */

/* Split "a.b[2].member" → { obj:"a.b[2]", member:"member" }.  Member names are
 * never indexed, so the final '.' always separates object path from member. */
function splitPath(path) {
    var i = path.lastIndexOf('.');
    if (i < 0) {
        throw new Error('safeConfig: path must address a member: ' + path);
    }
    return { obj: path.slice(0, i), member: path.slice(i + 1) };
}

function resolveLoc(serverName, locPath) {
    var srv = nginx.http.servers.find(function (s) {
        return s.name === serverName
            || (s.names && s.names.indexOf(serverName) >= 0);
    });
    if (!srv) { return null; }
    return srv.locations.find(function (l) { return l.path === locPath; });
}

function serverIndex(name) {
    var ss = nginx.http.servers;
    for (var i = 0; i < ss.length; i++) {
        if (ss[i].name === name
            || (ss[i].names && ss[i].names.indexOf(name) >= 0)) { return i; }
    }
    return -1;
}

function locIndex(si, locPath) {
    var ls = nginx.http.servers[si].locations;
    for (var j = 0; j < ls.length; j++) {
        if (ls[j].path === locPath) { return j; }
    }
    return -1;
}

function upstreamIndex(name) {
    var us = nginx.http.upstreams;
    for (var i = 0; i < us.length; i++) {
        if (us[i].name === name) { return i; }
    }
    return -1;
}

function peerIndex(ui, addr) {
    var ps = nginx.http.upstreams[ui].peers;
    for (var k = 0; k < ps.length; k++) {
        if (ps[k].address === addr) { return k; }
    }
    return -1;
}

/* ------------------------------------------------------------------ *
 * Core gateway                                                        *
 * ------------------------------------------------------------------ */

/*
 * apply(path, value, opts) — the single safe-mutation entry point.
 * Consults describe(), enforces the class gate, then applies + fans out.
 * Returns a plan object describing what was (or would be) done.
 */
function apply(path, value, opts) {
    opts = opts || {};

    var sp = splitPath(path);
    var d  = nginx.describe(sp.obj, sp.member);
    if (d === null) {
        throw new Error('safeConfig: not a classified settable member: ' + path);
    }

    /* Per-tier opt-in gate. */
    if (d.class === 'irreversible' && opts.confirm !== true) {
        throw new Error('safeConfig: "' + path + '" is irreversible'
            + (d.note ? ' (' + d.note + ')' : '')
            + '; pass {confirm:true} to proceed');
    }
    if (d.class === 'guarded' && opts.ack !== true) {
        throw new Error('safeConfig: "' + path + '" is guarded'
            + (d.note ? ' (' + d.note + ')' : '')
            + '; pass {ack:true} to proceed');
    }

    var fanOut = (d.propagation === 'worker-local');

    var plan = {
        path:          path,
        value:         value,
        'class':       d.class,
        propagation:   d.propagation,
        requestScoped: d.requestScoped,
        fanOut:        fanOut,
        note:          d.note || null
    };

    if (opts.dryRun) {
        plan.applied   = false;
        plan.fannedOut = false;
        return plan;
    }

    /* Apply in this worker. */
    nginx.set(path, value);

    /* Fan out to the others only when the write is worker-local. */
    if (fanOut && _fanout) {
        _fanout.postMessage({ type: 'op',
            cmd: { kind: 'set', path: path, value: value } });
        plan.fannedOut = true;
    } else {
        plan.fannedOut = false;
    }

    plan.applied = true;
    return plan;
}

function plan(path, value) {
    return apply(path, value, { dryRun: true });
}

/* ------------------------------------------------------------------ *
 * Named policies                                                      *
 * ------------------------------------------------------------------ */

/*
 * canaryWeight(upstream, pct) — shift pct% of traffic to the canary peer.
 * Convention: peers[0] is stable, peers[last] is the canary.  Weights are set
 * to (100-pct) and pct (clamped to >=1, since nginx weights must be positive).
 */
function canaryWeight(upstreamName, pct, opts) {
    if (typeof pct !== 'number' || pct < 0 || pct > 100) {
        throw new Error('canaryWeight: pct must be 0..100');
    }
    var ui = upstreamIndex(upstreamName);
    if (ui < 0) { throw new Error('canaryWeight: upstream not found: ' + upstreamName); }

    var peers = nginx.http.upstreams[ui].peers;
    if (peers.length < 2) {
        throw new Error('canaryWeight: needs >=2 peers (stable, canary)');
    }
    var ci = peers.length - 1;

    return {
        stable: apply('http.upstreams[' + ui + '].peers[0].weight',
                      Math.max(1, 100 - pct), opts),
        canary: apply('http.upstreams[' + ui + '].peers[' + ci + '].weight',
                      Math.max(1, pct), opts)
    };
}

/*
 * setResponseHeader(server, loc, key, value) — add/replace one response header
 * on a location (via headers.addHeaders).  Safe + worker-local → fanned out.
 */
function setResponseHeader(serverName, locPath, key, value, opts) {
    var si = serverIndex(serverName);
    if (si < 0) { throw new Error('setResponseHeader: server not found: ' + serverName); }
    var lj = locIndex(si, locPath);
    if (lj < 0) { throw new Error('setResponseHeader: location not found: ' + locPath); }

    var base = 'http.servers[' + si + '].locations[' + lj + ']';
    var loc  = nginx.http.servers[si].locations[lj];
    var cur  = loc.headers.addHeaders || [];

    var next = cur.filter(function (h) { return h.key !== key; });
    next.push({ key: key, value: value, always: false });

    return apply(base + '.headers.addHeaders', next, opts);
}

/*
 * toggleLocation(server, loc, on) — enable/disable a route.  Disabling installs
 * a 503 handler; enabling restores the original handler via clearHandler().
 * handler is 'guarded' → requires {ack:true}.  Fanned out via a 'toggle' command
 * (the handler function itself cannot be serialised).
 */
function toggleLocation(serverName, locPath, on, opts) {
    opts = opts || {};

    var si = serverIndex(serverName);
    if (si < 0) { throw new Error('toggleLocation: server not found: ' + serverName); }
    var lj = locIndex(si, locPath);
    if (lj < 0) { throw new Error('toggleLocation: location not found: ' + locPath); }

    var base = 'http.servers[' + si + '].locations[' + lj + ']';
    var d    = nginx.describe(base, 'handler');

    if (d.class === 'guarded' && opts.ack !== true) {
        throw new Error('toggleLocation: handler is guarded'
            + (d.note ? ' (' + d.note + ')' : '') + '; pass {ack:true} to proceed');
    }

    var plan = {
        path: base + '.handler', 'class': d.class, propagation: d.propagation,
        on: !!on, fanOut: true, note: d.note || null
    };

    if (opts.dryRun) {
        plan.applied = false; plan.fannedOut = false; return plan;
    }

    var loc = nginx.http.servers[si].locations[lj];
    applyToggle(loc, serverName, locPath, !!on);

    if (_fanout) {
        _fanout.postMessage({ type: 'op',
            cmd: { kind: 'toggle', serverName: serverName, locPath: locPath,
                   on: !!on } });
        plan.fannedOut = true;
    } else {
        plan.fannedOut = false;
    }

    plan.applied = true;
    return plan;
}

/*
 * drainPeer(upstream, addr) — gracefully mark a peer down across all workers.
 * down is 'safe' (reversible) and zoned-shared when the upstream has a zone.
 */
function drainPeer(upstreamName, addr, opts) {
    var ui = upstreamIndex(upstreamName);
    if (ui < 0) { throw new Error('drainPeer: upstream not found: ' + upstreamName); }
    var k = peerIndex(ui, addr);
    if (k < 0) { throw new Error('drainPeer: peer not found: ' + addr); }

    return apply('http.upstreams[' + ui + '].peers[' + k + '].down', true, opts);
}

/* ------------------------------------------------------------------ *
 * Install                                                             *
 * ------------------------------------------------------------------ */
nginx.safeConfig = {
    apply:             apply,
    plan:              plan,
    canaryWeight:      canaryWeight,
    setResponseHeader: setResponseHeader,
    toggleLocation:    toggleLocation,
    drainPeer:         drainPeer
};

})();
