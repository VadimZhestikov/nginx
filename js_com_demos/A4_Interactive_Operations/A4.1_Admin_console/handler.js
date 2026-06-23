// A4.1 — Snapshot-centric Admin Console (Layer 3)
//
// Composes all three layers of the operator-reconfiguration plan:
//   Layer 1  nginx.describe()  — every snapshot op is annotated with its
//                                safety class (🟢 safe / 🟡 guarded / 🔴 irreversible)
//   Layer 2  the safety gate   — applying a snapshot that contains irreversible
//                                ops is blocked unless explicitly confirmed
//   (engine) the A2.7 admin plugin — snapshots, apply/rollback/squash, reset-to-
//                                base, and SharedWorker fan-out (reused verbatim,
//                                plus a convergence stamp at the apply seam)
//
// The COW trap, made visible: each worker stamps the snapshot id it has applied
// into nginx.shared; the console's convergence panel shows which worker PIDs
// have caught up to the desired snapshot.
//
// REST (consumed by console.html):
//   GET  /                     the single-page console
//   GET  /c/snapshots          { snapshots, pinned, desired }
//   GET  /c/snapshot?id=       { id, ops:[ annotated… ] }
//   GET  /c/state              admin.state()
//   GET  /c/converge           { desired, workers:[…], allConverged }
//   POST /c/apply?id=[&confirm=1]   apply (blocked 409 if irreversible & !confirm)
//   POST /c/rollback           rollback one step
//   POST /c/squash?ids=a,b&name=    merge snapshots
//   POST /c/snapshot?name=     create snapshot from current state

import * as std from 'std';

// ── Load the A2.7 admin engine (snapshots / apply / rollback / squash) ───────
nginx.use('./admin-plugin', {
    keys: {
        'routes.products': '0',
        'canary.weight':   '0'
    },
    props: [
        { upstream: 'demo_backend', peer: '127.0.0.1:9991',
          property: 'weight', default: 5 },
        { upstream: 'demo_backend', peer: '127.0.0.1:9992',
          property: 'weight', default: 3 },
        { server: 'localhost', location: '/api/', subobject: 'headers',
          property: 'addHeaders', default: [] }
    ]
});

// Named handler referenced by addLocation structural-op snapshots.
nginx.admin.registerHandler('dynamicHandler', function (r) {
    r.respond(200, {'Content-Type': 'application/json'},
        JSON.stringify({ resource: 'dynamic', worker: r.variable('pid') }) + '\n');
});

// Second listener (port 8136), created at init-conf and attached to the same
// server.  Used to demonstrate reversible removeListener: a snapshot can
// soft-pause it (Host requests to :8136 stop being answered) and rollback
// resumes it — fanned out to every worker via the cfgworker.
var EXTRA_LISTENER = '127.0.0.1:8136';
nginx.http.attach(nginx.createSocket(EXTRA_LISTENER))
     .addServer(nginx.http.servers[0]);

// Load the SPA once at init; every worker inherits the string via COW.
var CONSOLE_HTML = std.loadFile(nginx.cycle.prefix + 'console.html') ||
                   '<!doctype html><h1>console.html not found</h1>';

nginx.broadcast(function () {
    var admin = nginx.admin;
    var srv   = nginx.http.servers[0];
    var locs  = srv.locations;

    function at(path, fn) {
        var l = locs.find(function (l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }
    function pid(r) { return r.variable('pid'); }
    function jsonOut(r, code, obj) {
        r.respond(code, {'Content-Type': 'application/json'},
                  JSON.stringify(obj) + '\n');
    }

    /* ---- op annotation via nginx.describe() (Layer 1) ------------------- */

    function resolvePropObj(desc) {
        if (desc.upstream !== undefined) {
            var u = nginx.http.upstreams.find(function (x) {
                return x.name === desc.upstream; });
            if (!u) { return null; }
            if (desc.peer !== undefined) {
                return u.peers.find(function (p) {
                    return p.address === desc.peer; }) || null;
            }
            return u;
        }
        if (desc.server !== undefined || desc.location !== undefined) {
            var s = nginx.http.servers.find(function (x) {
                return x.name === desc.server
                    || (x.names && x.names.indexOf(desc.server) >= 0); });
            if (!s) { return null; }
            var t = s;
            if (desc.location !== undefined) {
                t = s.locations.find(function (l) {
                    return l.path === desc.location; });
                if (!t) { return null; }
            }
            if (desc.subobject) { t = t[desc.subobject]; if (!t) { return null; } }
            return t;
        }
        return null;
    }

    /* Structural op → class (these are methods, classified by intent). */
    var STRUCT_CLASS = {
        addLocation:    'guarded',
        removeLocation: 'guarded',      /* reversible: tombstone + restoreLocation */
        restoreLocation:'guarded',
        removeServer:   'guarded',      /* reversible: tombstone + restoreServer (Track S) */
        restoreServer:  'guarded',
        removeListener: 'guarded',      /* reversible: soft pause + restoreListener (Track N) */
        restoreListener:'guarded',
        addServer:      'irreversible', /* cscf is never reclaimed from cycle->pool */
        addListener:    'irreversible'  /* socket bind; resource commit */
    };

    function annotate(op) {
        var a = { op: op, 'class': 'safe', propagation: 'worker-local',
                  reversible: true, label: '', note: null };

        if ('shared' in op) {
            a.kind = 'shared'; a.label = op.shared + ' = ' + op.value;
            a['class'] = 'safe'; a.propagation = 'auto-shared';

        } else if ('prop' in op) {
            a.kind = 'prop';
            var d   = op.prop;
            a.label = (d.upstream ? d.upstream + '/' + (d.peer || '') : d.server +
                       (d.location || '') + (d.subobject ? '.' + d.subobject : ''))
                      + '.' + d.property + ' = ' + JSON.stringify(op.value);
            var obj = resolvePropObj(d);
            var dsc = obj ? nginx.describe(obj, d.property) : null;
            if (dsc) {
                a['class'] = dsc['class']; a.propagation = dsc.propagation;
                a.reversible = dsc.reversible; a.note = dsc.note;
            }

        } else if ('op' in op) {
            a.kind = 'struct';
            a.label = op.op + ' ' + (op.serverName || '') + ' '
                    + (op.pattern || op.name || op.addr || '')
                    + (op.hard ? ' {hard}' : '');
            a['class'] = STRUCT_CLASS[op.op] || 'guarded';
            /* A hard removeListener closes the socket → irreversible. */
            if (op.op === 'removeListener' && op.hard) { a['class'] = 'irreversible'; }
            a.reversible = (a['class'] !== 'irreversible');

        } else if ('handler' in op) {
            a.kind = 'handler'; a.label = op.path + ' → ' + op.handler;
            a['class'] = 'guarded';

        } else if ('path' in op) {
            a.kind = 'path'; a.label = op.path + ' = ' + JSON.stringify(op.value);
            var i = op.path.lastIndexOf('.');
            if (i > 0) {
                var pd = nginx.describe(op.path.slice(0, i), op.path.slice(i + 1));
                if (pd) { a['class'] = pd['class']; a.propagation = pd.propagation;
                          a.reversible = pd.reversible; a.note = pd.note; }
            }
        }
        return a;
    }

    function readSnapshot(id) {
        var txt = std.loadFile(nginx.cycle.prefix + 'snapshots/' + id + '.json');
        if (!txt) { return null; }
        try { return JSON.parse(txt); } catch (e) { return null; }
    }

    /* ---- convergence (read nginx.shared stamps) ------------------------- */
    function convergence() {
        var desired = nginx.shared.get('sc.cv.desired') || 'base';
        var workers = [];
        var all = true;
        /* worker indices 0..N-1; N from cpu/workers — probe a generous range */
        for (var i = 0; i < 32; i++) {
            var at = nginx.shared.get('sc.cv.' + i);
            if (at === undefined) { continue; }
            var conv = (at === desired);
            if (!conv) { all = false; }
            workers.push({ idx: i, at: at, converged: conv });
        }
        return { desired: desired, workers: workers, allConverged: all };
    }

    /* ---- REST endpoints ------------------------------------------------- */

    at('/', function (r) {
        r.respond(200, {'Content-Type': 'text/html; charset=utf-8'}, CONSOLE_HTML);
    });

    at('/c/snapshots', function (r) {
        jsonOut(r, 200, {
            snapshots: admin.listSnapshots(),
            desired:   nginx.shared.get('sc.cv.desired') || 'base',
            worker:    pid(r)
        });
    });

    at('/c/snapshot', function (r) {
        var snap = readSnapshot(r.queryParams.id);
        if (!snap) { jsonOut(r, 404, { error: 'not found' }); return; }
        jsonOut(r, 200, {
            id:  snap.id,
            ops: (snap.ops || []).map(annotate)
        });
    });

    at('/c/state', function (r) {
        var st = admin.state(); st.worker = pid(r); jsonOut(r, 200, st);
    });

    at('/c/converge', function (r) {
        var c = convergence(); c.worker = pid(r); jsonOut(r, 200, c);
    });

    at('/c/apply', function (r) {
        try {
            var id   = r.queryParams.id;
            var snap = readSnapshot(id);
            if (!snap) { jsonOut(r, 404, { error: 'not found: ' + id }); return; }

            /* Layer 2 gate: block irreversible ops unless confirmed. */
            var risky = (snap.ops || []).map(annotate)
                          .filter(function (a) { return a['class'] === 'irreversible'; });
            if (risky.length && r.queryParams.confirm !== '1') {
                jsonOut(r, 409, { blocked: true, reason: 'irreversible ops',
                    ops: risky.map(function (a) { return a.label; }) });
                return;
            }
            admin.applySnapshot(id);
            jsonOut(r, 200, { applied: id, worker: pid(r) });
        } catch (e) { jsonOut(r, 500, { error: String(e.message || e) }); }
    });

    at('/c/rollback', function (r) {
        try { jsonOut(r, 200, { rolledBackTo: admin.rollback() || 'base',
                                worker: pid(r) }); }
        catch (e) { jsonOut(r, 500, { error: String(e.message || e) }); }
    });

    at('/c/squash', function (r) {
        try {
            var ids = (r.queryParams.ids || '').split(',').filter(Boolean);
            jsonOut(r, 200, { id: admin.squash(ids, r.queryParams.name) });
        } catch (e) { jsonOut(r, 500, { error: String(e.message || e) }); }
    });

    at('/c/snapshot/create', function (r) {
        try { jsonOut(r, 200, { id: admin.createSnapshot(r.queryParams.name) }); }
        catch (e) { jsonOut(r, 500, { error: String(e.message || e) }); }
    });

    /* Stage a snapshot from an explicit ops array (body or ?ops= JSON). */
    at('/c/raw', function (r) {
        try {
            var ops = JSON.parse(r.queryParams.ops || '[]');
            jsonOut(r, 200, { id: admin.createRawSnapshot(r.queryParams.name, ops) });
        } catch (e) { jsonOut(r, 500, { error: String(e.message || e) }); }
    });

    /* app routes referenced by managed props / structural ops */
    at('/api/', function (r) {
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({ resource: 'api', worker: pid(r) }) + '\n');
    });
    at('/worker', function (r) { jsonOut(r, 200, { worker: pid(r) }); });

    /* ghost.local virtual server — its /who answers "ghost"; after a
     * removeServer snapshot, Host: ghost.local falls through to localhost. */
    var ghost = nginx.http.servers.find(function (s) {
        return s.name === 'ghost.local'; });
    if (ghost) {
        var w = ghost.locations.find(function (l) { return l.path === '/who'; });
        if (w) { w.handler = function (r) { r.respond(200, {}, 'ghost\n'); }; }
    }
});
