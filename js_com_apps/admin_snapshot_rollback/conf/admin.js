import * as std from 'std';
import * as os  from 'os';

/*
 * admin.js — nginx dynamic-config admin with snapshot / rollback.
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
 *     "id":       "0001-my-snapshot",
 *     "ts":       1710000000,
 *     "peers": [
 *       { "upstream": "backend", "address": "127.0.0.1:8091", "weight": 10,
 *         "down": false }
 *     ],
 *     "handlers": [
 *       { "path": "/foo/", "handler": "myHandler" }
 *     ]
 *   }
 *
 * Handler names are resolved via nginx.admin.registerHandler(name, fn).
 * A null handler name means clearHandler() is called for that location.
 */

(function () {

var _base     = null;   /* base state captured at startup */
var _handlers = {};     /* name → function(req) */
var _pinnedId = null;   /* id of snapshot to apply on next restart */

/* ------------------------------------------------------------------ *
 * Helpers                                                             *
 * ------------------------------------------------------------------ */

function _findUpstream(name) {
    return nginx.http.upstreams.find(function (u) {
        return u.name === name;
    });
}

function _findLocation(path) {
    var loc = null;
    nginx.http.servers.forEach(function (srv) {
        srv.locations.forEach(function (l) {
            if (l.path === path) { loc = l; }
        });
    });
    return loc;
}

/* Snapshot directory — co-located with this script */
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
    /* os.readdir returns [names, err] */
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
 * Records the as-delivered upstream peer weights/down flags so that
 * applySnapshot can always reset to this baseline before applying a delta.
 */
function _captureBaseState() {
    var peers = [];

    nginx.http.upstreams.forEach(function (u) {
        u.peers.forEach(function (p) {
            peers.push({
                upstream: u.name,
                address:  p.address,
                weight:   p.weight,
                down:     !!p.down,
            });
        });
    });

    _base = { peers: peers };
}

/* ------------------------------------------------------------------ *
 * Sync helpers                                                        *
 * ------------------------------------------------------------------ */

/*
 * Apply a peers array (from a snapshot or from _base) to the live config.
 * Only weight and down are mutable; address is used for lookup only.
 */
function _syncPeers(peersArr) {
    if (!peersArr || !peersArr.length) { return; }

    peersArr.forEach(function (entry) {
        var u = _findUpstream(entry.upstream);
        if (!u) { return; }

        u.peers.forEach(function (p) {
            if (p.address !== entry.address) { return; }
            if (entry.weight !== undefined) { p.weight = entry.weight; }
            if (entry.down   !== undefined) { p.down   = !!entry.down; }
        });
    });
}

/*
 * Apply a handlers array from a snapshot.
 * Each entry: { "path": "/foo/", "handler": "name" | null }
 * null → clearHandler(); a registered name → location.handler = fn.
 */
function _syncLocations(handlersArr) {
    if (!handlersArr || !handlersArr.length) { return; }

    handlersArr.forEach(function (entry) {
        var loc = _findLocation(entry.path);
        if (!loc) { return; }

        if (entry.handler === null || entry.handler === undefined) {
            loc.clearHandler();
        } else {
            var fn = _handlers[entry.handler];
            if (!fn) {
                nginx.log('admin: unknown handler name: ' + entry.handler);
                return;
            }
            loc.handler = fn;
        }
    });
}

/* ------------------------------------------------------------------ *
 * Apply snapshot                                                      *
 * ------------------------------------------------------------------ */

/*
 * Reset to base state, then apply snap's deltas.
 * Called inside nginx.broadcast() so it runs on every worker.
 */
function _applySnapshot(snap) {
    /* 1. Reset peers to base */
    _syncPeers(_base.peers);

    /* 2. Apply snapshot deltas */
    if (snap) {
        _syncPeers(snap.peers);
        _syncLocations(snap.handlers);
    }
}

/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

var admin = {};

admin.registerHandler = function (name, fn) {
    _handlers[name] = fn;
};

admin.listSnapshots = function () {
    return _listFiles(_snapshotsDir).map(function (n) {
        return n.slice(0, -5);   /* strip .json */
    });
};

admin.state = function () {
    if (!_base) { return null; }
    var result = { peers: [], handlers: [] };

    nginx.http.upstreams.forEach(function (u) {
        u.peers.forEach(function (p) {
            /* find matching base entry */
            var base = null;
            _base.peers.forEach(function (b) {
                if (b.upstream === u.name && b.address === p.address) {
                    base = b;
                }
            });
            if (!base) { return; }
            if (p.weight !== base.weight || !!p.down !== base.down) {
                result.peers.push({
                    upstream: u.name,
                    address:  p.address,
                    weight:   p.weight,
                    down:     !!p.down,
                });
            }
        });
    });

    return result;
};

admin.createSnapshot = function (name) {
    if (!_base) { throw new Error('base state not captured'); }
    if (!name)  { throw new Error('snapshot name required'); }

    var existing = admin.listSnapshots();
    var seq = existing.length + 1;
    var id  = (seq < 10 ? '000' : seq < 100 ? '00' : seq < 1000 ? '0' : '') +
              seq + '-' + name;

    var snap = {
        id:       id,
        ts:       Math.floor(Date.now() / 1000),
        peers:    admin.state().peers,
        handlers: admin.state().handlers,
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

    var idx = _pinnedId ? list.indexOf(_pinnedId) : list.length;
    var prev = idx > 0 ? list[idx - 1] : null;

    if (prev) {
        admin.applySnapshot(prev);
    } else {
        /* Roll back to clean base state */
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

/* ------------------------------------------------------------------ *
 * Startup                                                             *
 * ------------------------------------------------------------------ */

_captureBaseState();

nginx.admin = admin;

})();
