// A2.6 — Runtime Route Enable/Disable, Instantly Visible to All Workers
//
// Demonstrates two complementary cross-worker coordination primitives:
//
//   nginx.broadcast(fn) — called at init-conf time — queues fn so that every
//   worker runs it during process startup.  This installs the *same* routing
//   logic in all four workers with a single call.
//
//   nginx.shared — a lock-free shared-memory KV store.  A write by any one
//   worker is immediately visible to all other workers without any reload,
//   signal, or IPC round-trip.
//
// Together they replicate the "nginx -s reload" pattern — activate a new
// route across the entire worker pool — without worker restart or in-flight
// connection drops.
//
// Standard nginx needs `nginx -s reload` to activate a new location — that
// respawns all workers and briefly interrupts in-flight connections.  Here
// the route is live in all four workers within microseconds of the admin
// request, with zero connection drops.
//
// Endpoints:
//   POST /admin/add-route/    — enable /api/v2/ on every worker (via shared mem)
//   POST /admin/remove-route/ — disable /api/v2/ on every worker (via shared mem)
//   GET  /api/v2/             — 200 while enabled, 404 while disabled
//   GET  /status/             — lists enabled routes + PID of responding worker

// nginx.broadcast(fn) in master/init context: fn is queued and run in every
// worker during init_process — before the first request is accepted.
nginx.broadcast(function () {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function set(path, fn) {
        var loc = locs.find(function (l) { return l.path === path; });
        if (loc) { loc.handler = fn; }
    }

    /* Seed the shared flag exactly once (first worker wins). */
    if (nginx.shared.get('routes.api_v2') === undefined) {
        nginx.shared.set('routes.api_v2', '0');
    }

    /* ------------------------------------------------------------------ *
     * POST /admin/add-route/
     *
     * Writes '1' to shared memory.  All four workers see the new value on
     * their next request — no IPC, no reload, no signal required.
     * ------------------------------------------------------------------ */
    set('/admin/add-route/', function (r) {
        nginx.shared.set('routes.api_v2', '1');
        r.respond(200, {}, 'added /api/v2/ — broadcast via nginx.shared to all workers\n');
    });

    /* ------------------------------------------------------------------ *
     * POST /admin/remove-route/
     * ------------------------------------------------------------------ */
    set('/admin/remove-route/', function (r) {
        nginx.shared.set('routes.api_v2', '0');
        r.respond(200, {}, 'removed /api/v2/ — broadcast via nginx.shared to all workers\n');
    });

    /* ------------------------------------------------------------------ *
     * GET /api/v2/
     *
     * Reads the shared flag on every request.  Each worker independently
     * decides — no coordinator, no lock — so the route is live or dead
     * on all workers the instant the flag is written.
     * ------------------------------------------------------------------ */
    set('/api/v2/', function (r) {
        if (nginx.shared.get('routes.api_v2') !== '1') {
            r.respond(404, {}, 'not found\n');
            return;
        }
        r.respond(200, {'content-type': 'application/json'},
            JSON.stringify({
                ok:     true,
                route:  '/api/v2/',
                worker: r.variable('pid')
            }) + '\n');
    });

    /* ------------------------------------------------------------------ *
     * GET /status/
     *
     * Returns the enabled route list and PID of the responding worker.
     * Sending several requests and observing different PIDs confirms each
     * worker independently reads the shared state.
     * ------------------------------------------------------------------ */
    set('/status/', function (r) {
        var enabled = nginx.shared.get('routes.api_v2') === '1';
        var routes  = enabled ? ['/api/v2/'] : [];

        r.respond(200, {'content-type': 'application/json'},
            JSON.stringify({ pid: r.variable('pid'), routes: routes }) + '\n');
    });
});
