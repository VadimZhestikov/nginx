// A2.7 — Admin Plugin: Snapshot / Rollback via nginx.use() + nginx.shared
//
// Demonstrates the three cross-worker coordination primitives working together:
//
//   nginx.use('./admin-plugin', config)
//       Packages the entire admin logic as a self-contained plugin.  Called in
//       master/init-conf context (P7), so the plugin is evaluated once and all
//       workers inherit the result via COW fork — no per-worker re-evaluation.
//
//   nginx.broadcast(fn)
//       Called inside the plugin at init-conf time to seed default flag values
//       into each worker's nginx.shared view before the first request arrives.
//
//   nginx.shared
//       The control plane.  Any worker can write; every other worker sees the
//       new value on its next nginx.shared.get() call — no IPC, no reload.
//       Snapshot apply and rollback write nginx.shared, so configuration
//       changes are visible to all four workers within microseconds.
//
// Managed shared-memory keys:
//   routes.products   — 0|1: enable the /api/products/ endpoint
//   routes.premium    — 0|1: enable the /api/premium/ endpoint
//   canary.weight     — 0-100: percentage of /api/ traffic routed to v2 handler
//
// Plugin REST API (mounted on /admin/):
//   GET  /admin/state             — current shared-key values
//   GET  /admin/snapshots         — list saved snapshot ids
//   POST /admin/snapshots         — create snapshot (body: {"name":"..."})
//   POST /admin/raw-snapshot      — explicit ops array (body: {"name":"...","ops":[...]})
//   GET  /admin/snapshots/:id     — snapshot JSON content
//   POST /admin/apply/:id         — apply a snapshot (fan-out to all workers via SW)
//   POST /admin/rollback          — rollback one step
//   POST /admin/set               — set one key (body: {"key":"...","value":"..."})
//   GET  /admin/worker            — responding worker PID
//
// App endpoints:
//   GET  /api/products/           — live while routes.products == "1"
//   GET  /api/premium/            — live while routes.premium == "1"
//   GET  /api/                    — routes to v1 or v2 handler per canary.weight
//   GET  /status/                 — worker PID + current flag snapshot
//   GET  /dynamic/                — added dynamically via addLocation snapshot

// ── 1. Load the admin plugin ─────────────────────────────────────────────────
//
// nginx.use() in master context (P7): evaluates admin-plugin/index.js once.
// All workers inherit nginx.admin and the /admin/ REST handler via COW fork.
// The plugin also calls nginx.broadcast() internally to seed nginx.shared
// defaults in every worker during init_process.
nginx.use('./admin-plugin', {
    keys: {
        'routes.products': '0',   /* disabled by default */
        'routes.premium':  '0',
        'canary.weight':   '0'    /* 0 = all traffic to v1 */
    }
});

// ── 2. Register named handlers for structural-op snapshots ───────────────────
//
// nginx.admin.registerHandler() runs here at init-conf time (master, before
// fork) so all workers inherit _handlers via COW.  The SharedWorker fan-out
// carries only the handler name string; each receiving worker resolves it
// locally — no closure serialisation required.
nginx.admin.registerHandler('dynamicHandler', function (r) {
    r.respond(200, {'Content-Type': 'application/json'},
        JSON.stringify({ resource: 'dynamic', worker: r.variable('pid') }) + '\n');
});

// ── 3. Install per-worker app route handlers ─────────────────────────────────
//
// nginx.broadcast(fn) here queues fn to run in every worker during init_process
// (after the plugin's own broadcast has seeded the shared keys).
// Each worker independently installs the same JS closures — reads nginx.shared
// on every request so changes from any worker are picked up immediately.
nginx.broadcast(function () {
    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function set(path, fn) {
        var l = locs.find(function (l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    /* /api/products/ — gated by routes.products flag */
    set('/api/products/', function (r) {
        if (nginx.shared.get('routes.products') !== '1') {
            r.respond(404, {}, 'not found\n');
            return;
        }
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                resource: 'products',
                items:    [{id: 1, name: 'Widget A'}, {id: 2, name: 'Widget B'}],
                worker:   r.variable('pid')
            }) + '\n');
    });

    /* /api/premium/ — gated by routes.premium flag */
    set('/api/premium/', function (r) {
        if (nginx.shared.get('routes.premium') !== '1') {
            r.respond(404, {}, 'not found\n');
            return;
        }
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                resource: 'premium',
                features: ['analytics', 'export', 'sso'],
                worker:   r.variable('pid')
            }) + '\n');
    });

    /* /api/ — canary: routes canary.weight% of requests to v2 handler */
    set('/api/', function (r) {
        var weight = parseInt(nginx.shared.get('canary.weight') || '0', 10);
        var useV2  = (Math.random() * 100) < weight;
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                version: useV2 ? 'v2' : 'v1',
                canary:  weight,
                worker:  r.variable('pid')
            }) + '\n');
    });

    /* /status/ — worker identity + all managed flag values */
    set('/status/', function (r) {
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify({
                worker:          r.variable('pid'),
                'routes.products': nginx.shared.get('routes.products'),
                'routes.premium':  nginx.shared.get('routes.premium'),
                'canary.weight':   nginx.shared.get('canary.weight')
            }) + '\n');
    });
});
