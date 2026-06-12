// cross-worker-admin plugin — nginx.use() entry point
//
// Adapted from js_com_apps/admin_snapshot_rollback/conf/admin.js + admin-api.js.
//
// Cross-worker propagation uses a SharedWorker as config authority (Strategy 1
// from js-reconfig-guide-all-workers):
//
//   1. The requesting worker applies the snapshot locally (immediate).
//   2. It posts {type:'apply', snap} to cfgWorker.
//   3. cfgWorker fans the message out to ALL connected workers via postMessage().
//   4. Each worker's cfgWorker.onmessage handler applies the same snapshot.
//
// This correctly propagates BOTH kinds of ops:
//
//   {shared: "key", value: "v"}
//       nginx.shared.set() — lock-free shared memory, readable by all workers
//       without any further IPC.  The fan-out is redundant but harmless.
//
//   {op: "addLocation"|"removeLocation", serverName, pattern [, handler]}
//       Per-worker nginx routing-tree mutation.  REQUIRES the SW fan-out —
//       nginx.shared cannot carry structural changes.
//       Handler names are resolved from _handlers (populated at init-conf time
//       via nginx.admin.registerHandler(), inherited by all workers via COW).
//
// nginx.pluginConfig (second arg to nginx.use()):
//   {
//     keys:  { "flag.name": "default_value", ... },
//     props: [ descriptor, ... ]   // optional COM scalar tracking
//   }
//
// prop descriptor shapes (all fields are stable names, NOT indices):
//   { upstream, property }                            — upstream scalar
//   { upstream, peer, property }                      — individual peer
//   { server, property }                              — server scalar
//   { server, location, property }                    — location scalar
//   { server, location, subobject, property }         — sub-object scalar
//   string                                            — raw nginx.set() path
//   Any descriptor may include an optional `default` value used by rollback.
//
// nginx.admin API:
//   state()                        — current nginx.shared + managed prop values
//   registerHandler(name, fn)      — register a named handler for op resolution
//   createSnapshot(name)           — save current nginx.shared + prop state
//   createRawSnapshot(name, ops)   — save an explicit ops array
//   listSnapshots()                — sorted snapshot list
//   applySnapshot(id)              — apply + fan out via SharedWorker
//   rollback()                     — apply previous snapshot + fan out
//   compactOps(ops [, base])       — deduplicate an ops list
//
// REST (mounted on /admin/):
//   GET  /admin/state             — current key-value + prop state
//   GET  /admin/snapshots         — list snapshot ids
//   POST /admin/snapshots         — create snapshot; body: {"name":"..."}
//   POST /admin/raw-snapshot      — create explicit; body: {"name":"...","ops":[...]}
//   GET  /admin/snapshots/:id     — snapshot JSON content
//   POST /admin/apply/:id         — apply snapshot
//   POST /admin/rollback          — rollback one step
//   POST /admin/set               — set one shared key; body: {"key":"...","value":"..."}
//   GET  /admin/worker            — responding worker PID

import * as std from 'std';
import * as os  from 'os';

(function () {

/* ------------------------------------------------------------------ *
 * Config from nginx.use() caller                                      *
 * ------------------------------------------------------------------ */

var cfg           = nginx.pluginConfig || {};
var _managed      = cfg.keys   || {};
var _managedProps = cfg.props  || [];   /* [{upstream,peer?,server?,location?,
                                            subobject?,property,default?}] */

/* ------------------------------------------------------------------ *
 * Named handler registry                                              *
 * ------------------------------------------------------------------ *
 * Handlers are registered at init-conf time via
 * nginx.admin.registerHandler(name, fn).  Because init-conf runs in
 * master before fork, all workers inherit _handlers via COW — so when
 * the SW fan-out triggers _applyOps in another worker, the name can be
 * resolved locally without any extra IPC.
 */
var _handlers = {};

/* ------------------------------------------------------------------ *
 * Config-authority SharedWorker                                       *
 * ------------------------------------------------------------------ */

var cfgWorker = new SharedWorker(nginx.cycle.prefix + 'admin-plugin/cfgworker.js');

/*
 * Register the per-worker fan-out receiver inside nginx.broadcast() so it
 * runs in each worker's event loop after fork (same pattern as A2.5).
 * Also send 'get' to catch up if this worker was restarted by the master.
 */
nginx.broadcast(function () {
    cfgWorker.onmessage = function (msg) {
        var type = msg.data.type;
        if (type === 'apply' || type === 'rollback' || type === 'sync') {
            _applyOps(msg.data.snap ? msg.data.snap.ops : _baseOps());
        }
    };
    cfgWorker.postMessage({ type: 'get' });
});

/* ------------------------------------------------------------------ *
 * Startup: seed managed keys with defaults (first worker wins)        *
 * ------------------------------------------------------------------ */

nginx.broadcast(function () {
    var keys = cfg.keys || {};
    Object.keys(keys).forEach(function (k) {
        if (nginx.shared.get(k) === undefined) {
            nginx.shared.set(k, keys[k]);
        }
    });
});

/* ------------------------------------------------------------------ *
 * Filesystem helpers                                                  *
 * ------------------------------------------------------------------ */

var _snapshotsDir = nginx.cycle.prefix + 'snapshots/';

function _snapshotPath(id) { return _snapshotsDir + id + '.json'; }

function _readFile(path) {
    try { return std.loadFile(path); } catch (e) { return null; }
}

function _writeFile(path, text) {
    var f = std.open(path, 'w');
    if (!f) { throw new Error('cannot open ' + path + ' for writing'); }
    f.puts(text);
    f.close();
}

function _listFiles(dir) {
    var res = os.readdir(dir);
    if (res[1] !== 0) { return []; }
    return res[0].filter(function (n) { return n.slice(-5) === '.json'; }).sort();
}

function _seqId(name) {
    var seq = admin.listSnapshots().length + 1;
    return (seq < 10 ? '000' : seq < 100 ? '00' : seq < 1000 ? '0' : '') + seq + '-' + name;
}

/* ------------------------------------------------------------------ *
 * Pinned-id helpers (stored in nginx.shared for cross-worker cursor)  *
 * ------------------------------------------------------------------ */

function _getPinned()   { return nginx.shared.get('admin.__pinned') || null; }
function _setPinned(id) {
    if (id === null) { nginx.shared.delete('admin.__pinned'); }
    else             { nginx.shared.set('admin.__pinned', id); }
}

/* ------------------------------------------------------------------ *
 * Named-descriptor helpers (Task 1 + 2)                              *
 * ------------------------------------------------------------------ *
 * Prop descriptors identify COM objects by stable names (upstream
 * name, peer address, server name, location path) rather than array
 * indices.  This makes snapshots survive addLocation / peer reordering.
 *
 * _propKey(desc)  — stable dedup key for compactOps
 * _propLabel(desc) — human-readable key for admin.state() display
 * _resolveTarget(desc) — walk the COM tree to the target object
 * _readProp(desc) — read the current live value of a described property
 */

function _propKey(desc) {
    if (typeof desc === 'string') { return desc; }
    return [
        desc.upstream  || '',
        desc.peer      || '',
        desc.server    || '',
        desc.location  || '',
        desc.subobject || '',
        desc.property  || ''
    ].join(':');
}

function _propLabel(desc) {
    if (typeof desc === 'string') { return desc; }
    var parts = [];
    if (desc.upstream)  { parts.push(desc.upstream); }
    if (desc.peer)      { parts.push(desc.peer); }
    if (desc.server)    { parts.push(desc.server); }
    if (desc.location)  { parts.push(desc.location); }
    if (desc.subobject) { parts.push(desc.subobject); }
    if (desc.property)  { parts.push(desc.property); }
    return parts.join('.');
}

/*
 * Resolve a descriptor to {target, property} where target is the COM
 * object that owns the property.  Returns null and logs on failure.
 */
function _resolveTarget(desc) {
    if (typeof desc === 'string') {
        /* Raw path — caller does nginx.get/set directly, no resolution needed */
        return { raw: desc };
    }

    if (desc.upstream !== undefined) {
        var ups = nginx.http.upstreams.find(function (u) {
            return u.name === desc.upstream;
        });
        if (!ups) {
            nginx.log(4, 'admin prop: upstream not found: ' + desc.upstream);
            return null;
        }
        var target = ups;
        if (desc.peer !== undefined) {
            target = ups.peers.find(function (p) {
                return p.address === desc.peer;
            });
            if (!target) {
                nginx.log(4, 'admin prop: peer not found: ' + desc.peer
                           + ' in upstream ' + desc.upstream);
                return null;
            }
        }
        return { target: target, property: desc.property };
    }

    if (desc.server !== undefined || desc.location !== undefined) {
        var srv = nginx.http.servers.find(function (s) {
            return s.name === desc.server
                || (s.names && s.names.indexOf(desc.server) >= 0);
        });
        if (!srv) {
            nginx.log(4, 'admin prop: server not found: ' + desc.server);
            return null;
        }
        var propTarget = srv;
        if (desc.location !== undefined) {
            propTarget = srv.locations.find(function (l) {
                return l.path === desc.location;
            });
            if (!propTarget) {
                nginx.log(4, 'admin prop: location not found: ' + desc.location
                           + ' on server ' + desc.server);
                return null;
            }
        }
        if (desc.subobject) {
            propTarget = propTarget[desc.subobject];
            if (!propTarget) {
                nginx.log(4, 'admin prop: subobject not found: ' + desc.subobject);
                return null;
            }
        }
        return { target: propTarget, property: desc.property };
    }

    nginx.log(4, 'admin prop: unrecognised descriptor: '
               + JSON.stringify(desc));
    return null;
}

/* Read the current live value of a named descriptor. */
function _readProp(desc) {
    if (typeof desc === 'string') { return nginx.get(desc); }
    var resolved = _resolveTarget(desc);
    if (!resolved) { return undefined; }
    return resolved.target[resolved.property];
}

/* ------------------------------------------------------------------ *
 * Ops application                                                     *
 * ------------------------------------------------------------------ *
 * Supports three op families:
 *
 *   {shared, value}
 *       nginx.shared.set(key, value) — instantly cross-worker.
 *
 *   {prop, value}
 *       COM scalar mutation via named descriptor (stable across index
 *       changes) or raw nginx.set() path string (backward compat).
 *       Propagation to other workers is the SharedWorker's job.
 *
 *   {op, ...}
 *       Structural mutations on this worker's nginx routing tree.
 *       Propagation to other workers is the SharedWorker's job.
 *       Supported: addLocation (with optional handler name),
 *                  removeLocation.
 */
function _applyOps(ops) {
    if (!ops || !ops.length) { return; }
    ops.forEach(function (op) {

        if ('shared' in op) {
            nginx.shared.set(op.shared, String(op.value));

        } else if ('prop' in op) {
            var desc = op.prop;
            if (typeof desc === 'string') {
                /* Backward-compat: raw path string */
                nginx.set(desc, op.value);
            } else {
                var resolved = _resolveTarget(desc);
                if (resolved) {
                    resolved.target[resolved.property] = op.value;
                }
            }

        } else if (op.op === 'addLocation') {
            var srvAdd = nginx.http.servers.find(function (s) {
                return s.name === op.serverName;
            });
            if (!srvAdd) {
                nginx.log(4, 'admin: addLocation: server not found: ' + op.serverName);
                return;
            }
            var loc = srvAdd.addLocation(op.pattern);
            if (op.handler) {
                var fn = _handlers[op.handler];
                if (fn) { loc.handler = fn; }
            }

        } else if (op.op === 'removeLocation') {
            var srvRm = nginx.http.servers.find(function (s) {
                return s.name === op.serverName;
            });
            if (srvRm) { srvRm.removeLocation(op.pattern); }
        }
    });
}

function _baseOps() {
    var ops = Object.keys(_managed).map(function (k) {
        return { shared: k, value: _managed[k] };
    });
    /* Include default values for managed props so rollback-to-base resets them */
    _managedProps.forEach(function (desc) {
        if ('default' in desc) {
            ops.push({ prop: desc, value: desc.default });
        }
    });
    return ops;
}

/*
 * compactOps(ops [, baseOps]) — last-write-wins deduplication.
 *
 * Handles three op families:
 *   {shared}  — dedup key 'S:<key>'
 *   {prop}    — dedup key 'P:<stable descriptor key>' (not index-based)
 *   {op,...}  — dedup key 'OP:<op>:<serverName>:<pattern>'
 *
 * Ops that are already equal to the baseOps value are elided.
 */
function compactOps(ops, baseOps) {
    if (!ops || !ops.length) { return []; }

    var baseVal = {};
    if (baseOps) {
        baseOps.forEach(function (b) {
            if ('shared' in b) { baseVal['S:' + b.shared]           = b.value; }
            if ('prop'   in b) { baseVal['P:' + _propKey(b.prop)]   = b.value; }
        });
    }

    var seen    = {};
    var compact = [];

    for (var i = ops.length - 1; i >= 0; i--) {
        var op  = ops[i];
        var key;
        if ('shared' in op) {
            key = 'S:' + op.shared;
        } else if ('prop' in op) {
            key = 'P:' + _propKey(op.prop);
        } else if (op.op) {
            key = 'OP:' + op.op + ':' + (op.serverName || '') + ':' + (op.pattern || '');
        } else {
            key = 'UNKNOWN:' + JSON.stringify(op);
        }

        if (seen[key]) { continue; }
        seen[key] = true;

        /* Elide ops that are already at the base value */
        if (('shared' in op || 'prop' in op)
                && (key in baseVal) && op.value === baseVal[key]) {
            continue;
        }

        compact.unshift(op);
    }
    return compact;
}

/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

var admin = {};

admin.registerHandler = function (name, fn) {
    _handlers[name] = fn;
};

admin.state = function () {
    var out = {};
    Object.keys(_managed).forEach(function (k) {
        out[k] = nginx.shared.get(k);
    });
    if (_managedProps.length > 0) {
        out.props = {};
        _managedProps.forEach(function (desc) {
            out.props[_propLabel(desc)] = _readProp(desc);
        });
    }
    return out;
};

admin.listSnapshots = function () {
    return _listFiles(_snapshotsDir).map(function (n) { return n.slice(0, -5); });
};

admin.createSnapshot = function (name) {
    if (!name) { throw new Error('snapshot name required'); }
    var id  = _seqId(name);
    var ops = Object.keys(_managed).map(function (k) {
        return { shared: k, value: nginx.shared.get(k) };
    });
    /* Capture live values of managed COM scalar properties */
    _managedProps.forEach(function (desc) {
        var val = _readProp(desc);
        if (val !== undefined) {
            ops.push({ prop: desc, value: val });
        }
    });
    var snap = { id: id, ts: Math.floor(Date.now() / 1000), ops: ops };
    _writeFile(_snapshotPath(id), JSON.stringify(snap, null, 2) + '\n');
    _setPinned(id);
    return id;
};

admin.createRawSnapshot = function (name, ops) {
    if (!name) { throw new Error('snapshot name required'); }
    if (!Array.isArray(ops)) { throw new Error('ops array required'); }
    var id   = _seqId(name);
    var snap = { id: id, ts: Math.floor(Date.now() / 1000), ops: ops };
    _writeFile(_snapshotPath(id), JSON.stringify(snap, null, 2) + '\n');
    _setPinned(id);
    return id;
};

/*
 * applySnapshot — apply locally then fan out via SharedWorker.
 *
 * nginx.shared ops are immediately visible to all workers regardless of
 * the fan-out, but structural ops (addLocation, removeLocation) REQUIRE
 * the fan-out to reach workers other than the one that received the POST.
 */
admin.applySnapshot = function (id) {
    var text = _readFile(_snapshotPath(id));
    if (!text) { throw new Error('snapshot not found: ' + id); }
    var snap;
    try { snap = JSON.parse(text); } catch (e) {
        throw new Error('snapshot parse error: ' + e.message);
    }

    /* Apply locally (immediate). */
    _applyOps(snap.ops);
    _setPinned(id);

    /* Fan out to all other workers via SharedWorker. */
    cfgWorker.postMessage({ type: 'apply', snap: snap });

    return id;
};

admin.rollback = function () {
    var list   = admin.listSnapshots();
    if (!list.length) { throw new Error('no snapshots available'); }
    var pinned = _getPinned();
    var idx    = pinned ? list.indexOf(pinned) : list.length;
    var prev   = idx > 0 ? list[idx - 1] : null;

    if (prev) {
        admin.applySnapshot(prev);
    } else {
        /* Reset to caller-supplied defaults. */
        _applyOps(_baseOps());
        _setPinned(null);
        cfgWorker.postMessage({ type: 'rollback', snap: null });
    }
    return prev;
};

admin.compactOps = compactOps;

nginx.admin = admin;

/* ------------------------------------------------------------------ *
 * REST handlers — mounted on /admin/                                  *
 * ------------------------------------------------------------------ */

var adminLoc = (function () {
    var found = null;
    nginx.http.servers.forEach(function (s) {
        s.locations.forEach(function (l) {
            if (l.path === '/admin/') { found = l; }
        });
    });
    return found;
}());

if (!adminLoc) {
    nginx.log(4, 'cross-worker-admin: /admin/ location not found; REST API disabled');
} else {
    adminLoc.handler = async function (r) {
        var uri = r.uri;
        var m   = r.method;

        function ok(data) {
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(data) + '\n');
        }
        function err(status, msg) {
            r.respond(status, {'Content-Type': 'application/json'},
                JSON.stringify({error: String(msg)}) + '\n');
        }
        function parseBody(raw) {
            if (!raw) { return {}; }
            try { return JSON.parse(raw); } catch (e) { return null; }
        }

        try {

            if (m === 'GET' && uri === '/admin/worker') {
                return ok({ worker: r.variable('pid') });
            }

            if (m === 'GET' && uri === '/admin/state') {
                return ok(admin.state());
            }

            if (m === 'GET' && uri === '/admin/snapshots') {
                return ok(admin.listSnapshots());
            }

            if (m === 'POST' && uri === '/admin/snapshots') {
                var body = parseBody(await r.readBody());
                if (!body) { return err(400, 'invalid JSON body'); }
                if (!body.name) { return err(400, 'name required'); }
                return ok({ id: admin.createSnapshot(body.name) });
            }

            /* POST /admin/raw-snapshot — explicit ops array */
            if (m === 'POST' && uri === '/admin/raw-snapshot') {
                var rawBody = parseBody(await r.readBody());
                if (!rawBody) { return err(400, 'invalid JSON body'); }
                if (!rawBody.name) { return err(400, 'name required'); }
                if (!Array.isArray(rawBody.ops)) { return err(400, 'ops array required'); }
                return ok({ id: admin.createRawSnapshot(rawBody.name, rawBody.ops) });
            }

            var snapGet = uri.match(/^\/admin\/snapshots\/([^\/]+)$/);
            if (m === 'GET' && snapGet) {
                var text = _readFile(_snapshotPath(snapGet[1]));
                if (!text) { return err(404, 'snapshot not found: ' + snapGet[1]); }
                r.respond(200, {'Content-Type': 'application/json'}, text);
                return;
            }

            var applyMatch = uri.match(/^\/admin\/apply\/([^\/]+)$/);
            if (m === 'POST' && applyMatch) {
                return ok({ applied: admin.applySnapshot(applyMatch[1]) });
            }

            if (m === 'POST' && uri === '/admin/rollback') {
                return ok({ rolledBackTo: admin.rollback() || 'base' });
            }

            if (m === 'POST' && uri === '/admin/set') {
                var setBody = parseBody(await r.readBody());
                if (!setBody || !setBody.key) { return err(400, 'key required'); }
                nginx.shared.set(setBody.key, String(setBody.value));
                return ok({ key: setBody.key, value: setBody.value });
            }

            err(404, 'unknown: ' + m + ' ' + uri);

        } catch (e) {
            err(500, e.message || String(e));
        }
    };
}

})();
