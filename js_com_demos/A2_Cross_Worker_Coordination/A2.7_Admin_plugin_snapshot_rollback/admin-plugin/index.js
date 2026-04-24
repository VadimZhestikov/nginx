// cross-worker-admin plugin — nginx.use() entry point
//
// Adapted from js_com_apps/admin_snapshot_rollback/conf/admin.js + admin-api.js.
//
// Exposes nginx.admin with snapshot / rollback API and mounts REST handlers
// on the /admin/ location.
//
// Snapshot ops come in two tiers that reflect how the JS COM propagates changes
// across nginx workers:
//
//   {shared: "key", value: "v"}
//       Writes nginx.shared.set(key, v).  nginx.shared is a lock-free
//       shared-memory segment visible to ALL workers instantly — no IPC,
//       no reload.  Route-toggle decisions read back from nginx.shared on
//       every request, so the change takes effect the moment the write lands.
//
//   {path: "http.upstreams[i].peers[j].weight", value: N}
//       Calls nginx.set(path, N) — upstream peer scalars live in each
//       worker's own COW copy.  Applying this op from a request handler
//       changes only the worker that receives the POST.  Included because
//       it is part of the original ops-list format; production use would
//       add a coordinated reload step for peer-property snapshots.
//
// nginx.pluginConfig (passed from the caller's nginx.use() second argument):
//   {
//     keys: { "flag.name": "default_value", ... }   — shared keys to manage
//   }
//
// nginx.admin API:
//   state()                    — current values of all managed shared keys
//   createSnapshot(name)       — persist current state as a numbered snapshot
//   createRawSnapshot(name, ops) — persist an explicit ops array
//   listSnapshots()            — sorted list of snapshot ids
//   applySnapshot(id)          — apply a saved snapshot (nginx.shared ops: instant)
//   rollback()                 — apply the previous snapshot (or base defaults)
//   compactOps(ops[, base])    — deduplicate an ops list (last-write-wins)
//
// REST endpoints (mounted on /admin/):
//   GET  /admin/state             — current key-value state (JSON)
//   GET  /admin/snapshots         — list snapshot ids (JSON array)
//   POST /admin/snapshots         — create snapshot; body: {"name":"..."}
//   GET  /admin/snapshots/:id     — snapshot JSON content
//   POST /admin/apply/:id         — apply snapshot
//   POST /admin/rollback          — rollback one step
//   POST /admin/set               — set a shared key; body: {"key":"...","value":"..."}
//   GET  /admin/worker            — responding worker PID (for multi-worker tests)

import * as std from 'std';
import * as os  from 'os';

(function () {

/* ------------------------------------------------------------------ *
 * Config from nginx.use() caller                                      *
 * ------------------------------------------------------------------ */

var cfg      = nginx.pluginConfig || {};
var _managed = cfg.keys || {};   /* { key: defaultValue } */

/* _pinnedId is stored in nginx.shared so ALL workers share the same pointer. */
function _getPinned()  { return nginx.shared.get('admin.__pinned') || null; }
function _setPinned(id) {
    if (id === null) { nginx.shared.delete('admin.__pinned'); }
    else             { nginx.shared.set('admin.__pinned', id); }
}

/* ------------------------------------------------------------------ *
 * Filesystem helpers (same pattern as admin.js)                       *
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
 * Startup: seed managed keys with defaults (first worker wins)        *
 * ------------------------------------------------------------------ */

/*
 * nginx.broadcast(fn) in master/init-conf context queues fn to run in every
 * worker during init_process — before the first request is accepted.
 * This seeds each worker's nginx.shared view with the caller-supplied defaults
 * and also re-applies the pinned snapshot (cross-restart persistence).
 */
nginx.broadcast(function () {
    var keys = cfg.keys || {};
    Object.keys(keys).forEach(function (k) {
        if (nginx.shared.get(k) === undefined) {
            nginx.shared.set(k, keys[k]);
        }
    });
});

/* ------------------------------------------------------------------ *
 * Ops helpers                                                         *
 * ------------------------------------------------------------------ */

/*
 * _applyOps(ops) — apply an ops array to the live configuration.
 *
 * {shared, value} — nginx.shared.set(): immediately visible to ALL workers.
 * {path, value}   — nginx.set(): peer scalar, applies to THIS worker only.
 *                   In a multi-worker setup these require coordinated reload
 *                   for full propagation; included for format completeness.
 */
function _applyOps(ops) {
    if (!ops || !ops.length) { return; }
    ops.forEach(function (op) {
        if ('shared' in op) {
            nginx.shared.set(op.shared, op.value);
        } else if ('path' in op) {
            try { nginx.set(op.path, op.value); } catch (e) {
                nginx.log(4, 'admin: set ' + op.path + ' failed: ' + e.message);
            }
        }
    });
}

/*
 * compactOps(ops [, baseOps]) — deduplicate an ops list (from admin.js).
 *
 *   1. Last-write-wins per key/path.
 *   2. Identity removal vs baseOps (skip ops that restore to base).
 */
function compactOps(ops, baseOps) {
    if (!ops || !ops.length) { return []; }

    var baseVal = {};
    if (baseOps) {
        baseOps.forEach(function (b) {
            var k = 'shared' in b ? b.shared : b.path;
            if (k !== undefined) { baseVal[k] = b.value; }
        });
    }

    var seen    = {};
    var compact = [];

    for (var i = ops.length - 1; i >= 0; i--) {
        var op  = ops[i];
        var key = 'shared' in op ? 'S:' + op.shared : 'P:' + op.path;
        if (seen[key]) { continue; }
        seen[key] = true;

        var rawKey = 'shared' in op ? op.shared : op.path;
        if (rawKey !== undefined && (rawKey in baseVal) && op.value === baseVal[rawKey]) {
            continue;
        }

        compact.unshift(op);
    }
    return compact;
}

/* ------------------------------------------------------------------ *
 * Base state                                                          *
 * ------------------------------------------------------------------ */

/* Base = caller-supplied defaults (before any snapshot has been applied). */
function _baseOps() {
    return Object.keys(_managed).map(function (k) {
        return { shared: k, value: _managed[k] };
    });
}

/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

var admin = {};

/*
 * state() — current values of all managed shared keys.
 * Since nginx.shared is lock-free shared memory, this reflects the live
 * values written by any worker, regardless of which worker handles this call.
 */
admin.state = function () {
    var out = {};
    Object.keys(_managed).forEach(function (k) {
        out[k] = nginx.shared.get(k);
    });
    return out;
};

admin.listSnapshots = function () {
    return _listFiles(_snapshotsDir).map(function (n) { return n.slice(0, -5); });
};

/*
 * createSnapshot(name) — save current nginx.shared values as a snapshot.
 * The ops array captures each managed key's current value.
 */
admin.createSnapshot = function (name) {
    if (!name) { throw new Error('snapshot name required'); }
    var id  = _seqId(name);
    var ops = Object.keys(_managed).map(function (k) {
        return { shared: k, value: nginx.shared.get(k) };
    });
    var snap = { id: id, ts: Math.floor(Date.now() / 1000), ops: ops };
    _writeFile(_snapshotPath(id), JSON.stringify(snap, null, 2) + '\n');
    _setPinned(id);
    return id;
};

/*
 * createRawSnapshot(name, ops) — persist an explicit ops array (from admin.js).
 * Useful for building snapshots programmatically.
 */
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
 * applySnapshot(id) — apply a saved snapshot.
 *
 * {shared} ops write nginx.shared — the write is immediately visible to every
 * other worker without any nginx.broadcast() call.  This is the key difference
 * from peer-scalar ops: shared memory IS the broadcast.
 */
admin.applySnapshot = function (id) {
    var text = _readFile(_snapshotPath(id));
    if (!text) { throw new Error('snapshot not found: ' + id); }
    var snap;
    try { snap = JSON.parse(text); } catch (e) {
        throw new Error('snapshot parse error: ' + e.message);
    }
    _applyOps(snap.ops);
    _setPinned(id);
    return id;
};

/*
 * rollback() — apply the snapshot before the current one, or base defaults.
 */
admin.rollback = function () {
    var list   = admin.listSnapshots();
    if (!list.length) { throw new Error('no snapshots available'); }
    var pinned = _getPinned();
    var idx    = pinned ? list.indexOf(pinned) : list.length;
    var prev   = idx > 0 ? list[idx - 1] : null;
    if (prev) {
        admin.applySnapshot(prev);
    } else {
        /* Roll back to the caller-supplied defaults. */
        _applyOps(_baseOps());
        _setPinned(null);
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

            /* GET /admin/worker — worker identity (for multi-worker tests) */
            if (m === 'GET' && uri === '/admin/worker') {
                return ok({ worker: r.variable('pid') });
            }

            /* GET /admin/state */
            if (m === 'GET' && uri === '/admin/state') {
                return ok(admin.state());
            }

            /* GET /admin/snapshots */
            if (m === 'GET' && uri === '/admin/snapshots') {
                return ok(admin.listSnapshots());
            }

            /* POST /admin/snapshots — create */
            if (m === 'POST' && uri === '/admin/snapshots') {
                var body = parseBody(await r.readBody());
                if (!body) { return err(400, 'invalid JSON body'); }
                var name = body.name;
                if (!name) { return err(400, 'name required'); }
                return ok({ id: admin.createSnapshot(name) });
            }

            /* GET /admin/snapshots/:id */
            var snapGet = uri.match(/^\/admin\/snapshots\/([^\/]+)$/);
            if (m === 'GET' && snapGet) {
                var text = _readFile(_snapshotPath(snapGet[1]));
                if (!text) { return err(404, 'snapshot not found: ' + snapGet[1]); }
                r.respond(200, {'Content-Type': 'application/json'}, text);
                return;
            }

            /* POST /admin/apply/:id */
            var applyMatch = uri.match(/^\/admin\/apply\/([^\/]+)$/);
            if (m === 'POST' && applyMatch) {
                return ok({ applied: admin.applySnapshot(applyMatch[1]) });
            }

            /* POST /admin/rollback */
            if (m === 'POST' && uri === '/admin/rollback') {
                return ok({ rolledBackTo: admin.rollback() || 'base' });
            }

            /* POST /admin/set — set a single shared key directly */
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
