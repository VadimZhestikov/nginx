import * as std from 'std';
import * as os  from 'os';

/*
 * admin.js — nginx dynamic-config admin with ops-list snapshots.
 *
 * Exposes nginx.admin with:
 *
 *   nginx.admin.state()                   — current live config delta vs base
 *   nginx.admin.createSnapshot(name)      — persist current delta as a snapshot
 *   nginx.admin.listSnapshots()           — sorted list of snapshot ids
 *   nginx.admin.applySnapshot(id)         — reset to base + apply named snapshot
 *   nginx.admin.rollback()                — roll back to the previous snapshot
 *   nginx.admin.pin(id)                   — remember id for next restart
 *   nginx.admin.registerHandler(name, fn) — register a named JS handler
 *
 * Snapshot JSON format:
 *   {
 *     "id":  "0001-my-snapshot",
 *     "ts":  1710000000,
 *     "ops": [
 *       {"path": "http.upstreams[0].peers[0].weight", "value": 10},
 *       {"path": "http.upstreams[0].peers[1].down",   "value": true},
 *       {"path": "/foo/", "handler": "myHandler"}
 *     ]
 *   }
 *
 * Each op is either:
 *   {path, value}     — set a property via nginx.set(path, value)
 *   {path, handler}   — install/clear a named JS handler on a location
 *
 * Handler names are resolved via nginx.admin.registerHandler(name, fn).
 * A null handler means clearHandler() is called for that location.
 *
 * Base-state capture enumerates all settable properties of every upstream peer
 * via nginx.settable(), so the diff automatically covers any peer scalar
 * (weight, down, maxFails, failTimeout, maxConns).
 */

(function () {

var _base     = null;   /* [{path, value}] — baseline of all settable peer props */
var _handlers = {};     /* name → function(req) */
var _pinnedId = null;   /* id of snapshot to apply on next restart */

/* ------------------------------------------------------------------ *
 * Helpers                                                             *
 * ------------------------------------------------------------------ */

function _findLocation(locPath) {
    var loc = null;
    nginx.http.servers.forEach(function (srv) {
        srv.locations.forEach(function (l) {
            if (l.path === locPath) { loc = l; }
        });
    });
    return loc;
}

var _snapshotsDir = nginx.cycle.prefix + 'snapshots/';

function _snapshotPath(id) {
    return _snapshotsDir + id + '.json';
}

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
    return res[0].filter(function (n) {
        return n.slice(-5) === '.json';
    }).sort();
}

/* ------------------------------------------------------------------ *
 * Base-state capture                                                  *
 * ------------------------------------------------------------------ */

/*
 * _captureBaseState() — called once at init_conf time.
 *
 * Walks every upstream peer and records each settable property value as a
 * {path, value} entry.  nginx.settable(peer) returns the canonical list
 * (weight, maxFails, down, failTimeout, maxConns) so no hardcoding is needed.
 */
function _captureBaseState() {
    var ops = [];

    nginx.http.upstreams.forEach(function (u, ui) {
        u.peers.forEach(function (p, pi) {
            var base = 'http.upstreams[' + ui + '].peers[' + pi + ']';
            nginx.settable(p).forEach(function (prop) {
                ops.push({ path: base + '.' + prop, value: p[prop] });
            });
        });
    });

    _base = ops;
}

/* ------------------------------------------------------------------ *
 * Ops helpers                                                         *
 * ------------------------------------------------------------------ */

/*
 * _getDelta() — compare every tracked path against its base value.
 * Returns only the entries that differ (the ops-list diff).
 */
function _getDelta() {
    var delta = [];
    _base.forEach(function (entry) {
        var cur = nginx.get(entry.path);
        if (cur !== entry.value) {
            delta.push({ path: entry.path, value: cur });
        }
    });
    return delta;
}

/*
 * _applyOps(ops) — apply an ops-list to the live configuration.
 * {path, value}   → nginx.set(path, value)
 * {path, handler} → install or clear the named handler on the location
 */
function _applyOps(ops) {
    if (!ops || !ops.length) { return; }
    ops.forEach(function (op) {
        if ('handler' in op) {
            var loc = _findLocation(op.path);
            if (!loc) {
                nginx.log('admin: location not found: ' + op.path);
                return;
            }
            if (op.handler === null || op.handler === undefined) {
                loc.clearHandler();
            } else {
                var fn = _handlers[op.handler];
                if (!fn) {
                    nginx.log('admin: unknown handler name: ' + op.handler);
                    return;
                }
                loc.handler = fn;
            }
        } else {
            nginx.set(op.path, op.value);
        }
    });
}

/* ------------------------------------------------------------------ *
 * Apply snapshot                                                      *
 * ------------------------------------------------------------------ */

/*
 * _applySnapshot(snap) — reset to base, then apply snap's ops.
 * Called inside nginx.broadcast() so it runs on every worker.
 *
 * Supports both the current ops-list format ({id, ts, ops: [...]}) and
 * the legacy format ({id, ts, peers: [...], handlers: [...]}).
 */
function _applySnapshot(snap) {
    /* 1. Reset all tracked paths to base */
    _applyOps(_base);

    if (!snap) { return; }

    /* 2a. New format: ops-list */
    if (snap.ops) {
        _applyOps(snap.ops);
        return;
    }

    /* 2b. Legacy format: peers + handlers arrays */
    if (snap.peers) {
        snap.peers.forEach(function (entry) {
            var u = nginx.http.upstreams.find(function (u) {
                return u.name === entry.upstream;
            });
            if (!u) { return; }
            u.peers.forEach(function (p) {
                if (p.address !== entry.address) { return; }
                if (entry.weight !== undefined) { p.weight = entry.weight; }
                if (entry.down   !== undefined) { p.down   = !!entry.down; }
            });
        });
    }

    if (snap.handlers) {
        snap.handlers.forEach(function (entry) {
            var loc = _findLocation(entry.path);
            if (!loc) { return; }
            if (entry.handler === null || entry.handler === undefined) {
                loc.clearHandler();
            } else {
                var fn = _handlers[entry.handler];
                if (fn) { loc.handler = fn; }
            }
        });
    }
}

/* ------------------------------------------------------------------ *
 * Diff compaction                                                     *
 * ------------------------------------------------------------------ */

/*
 * compactOps(ops [, baseOps]) — reduce an ops-list by applying these rules:
 *
 *   1. Last-write-wins: if the same path appears multiple times, keep only
 *      the last op for that path (earlier ones are shadowed).
 *
 *   2. Identity removal: if a {path,value} op sets the value to the same
 *      value already recorded in baseOps, the op is a no-op and is removed.
 *      (Only applies when baseOps is supplied.)
 *
 * Handler ops {path, handler} are deduplicated by location path (last wins).
 * Passing baseOps = _base removes ops that restore to the baseline.
 */
function compactOps(ops, baseOps) {
    if (!ops || !ops.length) { return []; }

    /* Build a base-value lookup keyed by path */
    var baseVal = {};
    if (baseOps) {
        baseOps.forEach(function (b) {
            if ('value' in b) { baseVal[b.path] = b.value; }
        });
    }

    /* Walk in reverse; keep the LAST occurrence of each path */
    var seen    = {};
    var compact = [];

    for (var i = ops.length - 1; i >= 0; i--) {
        var op  = ops[i];
        var key = ('handler' in op) ? ('H:' + op.path) : ('V:' + op.path);

        if (seen[key]) { continue; }   /* earlier duplicate — skip */
        seen[key] = true;

        /* Identity removal for value ops */
        if ('value' in op && (op.path in baseVal)
            && op.value === baseVal[op.path])
        {
            continue;   /* restores to base — no-op */
        }

        compact.unshift(op);            /* preserve original order */
    }

    return compact;
}

/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

var admin = {};

/* admin.options — tunables */
admin.options = {
    compact: false,   /* auto-compact ops on createSnapshot */
};

admin.registerHandler = function (name, fn) {
    _handlers[name] = fn;
};

admin.listSnapshots = function () {
    return _listFiles(_snapshotsDir).map(function (n) {
        return n.slice(0, -5);   /* strip .json */
    });
};

/*
 * admin.state() — return the current live config delta vs base as an ops-list.
 * The returned object has a `peers` field for backward compatibility with
 * tooling that expects the old format, plus `ops` for the new format.
 */
admin.state = function () {
    if (!_base) { return null; }

    var delta = _getDelta();

    /* Backward-compat: also build a peers array from delta ops */
    var peersMap = {};
    delta.forEach(function (op) {
        /* Parse "http.upstreams[ui].peers[pi].prop" */
        var m = op.path.match(/^http\.upstreams\[(\d+)\]\.peers\[(\d+)\]\.(\w+)$/);
        if (!m) { return; }
        var key = m[1] + ',' + m[2];
        if (!peersMap[key]) {
            var u = nginx.http.upstreams[parseInt(m[1], 10)];
            var p = u ? u.peers[parseInt(m[2], 10)] : null;
            peersMap[key] = {
                upstream: u ? u.name : '?',
                address:  p ? p.address : '?',
            };
        }
        peersMap[key][m[3]] = op.value;
    });

    var peers = [];
    Object.keys(peersMap).forEach(function (k) { peers.push(peersMap[k]); });

    return { ops: delta, peers: peers };
};

admin.createSnapshot = function (name) {
    if (!_base) { throw new Error('base state not captured'); }
    if (!name)  { throw new Error('snapshot name required'); }

    var existing = admin.listSnapshots();
    var seq = existing.length + 1;
    var id  = (seq < 10 ? '000' : seq < 100 ? '00' : seq < 1000 ? '0' : '') +
              seq + '-' + name;

    var ops = _getDelta();
    if (admin.options.compact) {
        ops = compactOps(ops, _base);
    }

    var snap = {
        id:  id,
        ts:  Math.floor(Date.now() / 1000),
        ops: ops,
    };

    _writeFile(_snapshotPath(id), JSON.stringify(snap, null, 2) + '\n');
    _pinnedId = id;
    return id;
};

admin.applySnapshot = function (id) {
    var text = _readFile(_snapshotPath(id));
    if (!text) { throw new Error('snapshot not found: ' + id); }

    var snap;
    try { snap = JSON.parse(text); } catch (e) {
        throw new Error('snapshot parse error: ' + e.message);
    }

    nginx.broadcast(function () {
        _applySnapshot(snap);
    });

    _pinnedId = id;
    return id;
};

admin.rollback = function () {
    var list = admin.listSnapshots();
    if (!list.length) { throw new Error('no snapshots available'); }

    var idx  = _pinnedId ? list.indexOf(_pinnedId) : list.length;
    var prev = idx > 0 ? list[idx - 1] : null;

    if (prev) {
        admin.applySnapshot(prev);
    } else {
        nginx.broadcast(function () { _applySnapshot(null); });
        _pinnedId = null;
    }

    return prev;
};

admin.pin = function (id) {
    if (id !== null) {
        var text = _readFile(_snapshotPath(id));
        if (!text) { throw new Error('snapshot not found: ' + id); }
    }
    _pinnedId = id;
};

/*
 * admin.compactSnapshot(id) — rewrite a snapshot's ops-list in place,
 * removing redundant ops (last-write-wins + identity removal vs base).
 * Returns the number of ops removed.
 */
admin.compactSnapshot = function (id) {
    var text = _readFile(_snapshotPath(id));
    if (!text) { throw new Error('snapshot not found: ' + id); }

    var snap;
    try { snap = JSON.parse(text); } catch (e) {
        throw new Error('snapshot parse error: ' + e.message);
    }

    var before  = snap.ops ? snap.ops.length : 0;
    snap.ops    = compactOps(snap.ops || [], _base);
    var removed = before - snap.ops.length;

    _writeFile(_snapshotPath(id), JSON.stringify(snap, null, 2) + '\n');
    return removed;
};

/*
 * admin.squash(ids, name) — merge multiple snapshots into a single new
 * snapshot that encodes the net effect of applying them in sequence.
 *
 * The merged ops-list is built by concatenating all ops and then compacting
 * (last-write-wins per path, identity removal).  Returns the new snapshot id.
 */
admin.squash = function (ids, name) {
    if (!ids || !ids.length) { throw new Error('squash: ids array required'); }
    if (!name)               { throw new Error('squash: name required'); }

    /* Collect all ops in sequence */
    var allOps = [];
    ids.forEach(function (id) {
        var text = _readFile(_snapshotPath(id));
        if (!text) { throw new Error('squash: snapshot not found: ' + id); }
        var snap;
        try { snap = JSON.parse(text); } catch (e) {
            throw new Error('squash: parse error in ' + id + ': ' + e.message);
        }
        if (snap.ops) {
            snap.ops.forEach(function (op) { allOps.push(op); });
        }
    });

    /* Compact: last-write-wins + identity removal */
    var compacted = compactOps(allOps, _base);

    var existing = admin.listSnapshots();
    var seq = existing.length + 1;
    var id  = (seq < 10 ? '000' : seq < 100 ? '00' : seq < 1000 ? '0' : '') +
              seq + '-' + name;

    var snap = {
        id:  id,
        ts:  Math.floor(Date.now() / 1000),
        ops: compacted,
    };

    _writeFile(_snapshotPath(id), JSON.stringify(snap, null, 2) + '\n');
    _pinnedId = id;
    return id;
};

/* ------------------------------------------------------------------ *
 * Startup                                                             *
 * ------------------------------------------------------------------ */

_captureBaseState();

nginx.admin = admin;

})();
