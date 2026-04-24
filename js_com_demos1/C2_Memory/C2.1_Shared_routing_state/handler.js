// C2.1 — Shared routing state across all workers via nginx.shared
//
// nginx.shared is a cross-worker key-value store.  Any worker can write a
// route mapping; all other workers read it instantly without any IPC call.
// This demo shows a mutable routing table that all 2 workers see consistently.
//
// /admin/set-route/?path=<url>&backend=<name>  — write a route
// /route/ with X-Path header                   — read the route for that path
// /admin/list-routes/                           — dump all stored routes

(function () {
    var server = nginx.http.servers[0];
    var ROUTE_PREFIX = 'route:';

    // ── /admin/set-route/ ─────────────────────────────────────────────────
    var setLoc = server.findLocation('/admin/set-route/');
    setLoc.handler = function (r) {
        // Parse query string manually (QuickJS has no URLSearchParams)
        var path    = null;
        var backend = null;
        r.args.split('&').forEach(function (pair) {
            var kv = pair.split('=');
            if (kv[0] === 'path')    path    = decodeURIComponent(kv[1] || '');
            if (kv[0] === 'backend') backend = decodeURIComponent(kv[1] || '');
        });

        if (!path || !backend) {
            r.respond(400, {}, 'usage: /admin/set-route/?path=/api/&backend=v2\n');
            return;
        }

        nginx.shared.set(ROUTE_PREFIX + path, backend);
        r.respond(200, {}, 'route set: ' + path + ' → ' + backend + '\n');
    };

    // ── /route/ ───────────────────────────────────────────────────────────
    var routeLoc = server.findLocation('/route/');
    routeLoc.handler = function (r) {
        var path = r.headers['X-Path'] || r.headers['x-path'] || '/';
        var backend = nginx.shared.get(ROUTE_PREFIX + path);
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({
                path:    path,
                backend: backend || 'default'
            }) + '\n');
    };

    // ── /admin/list-routes/ ───────────────────────────────────────────────
    // nginx.shared doesn't have a keys() method, so we track known paths
    // in a separate shared key as a JSON array.
    var listLoc = server.findLocation('/admin/list-routes/');
    listLoc.handler = function (r) {
        // We'll enumerate a fixed set of well-known paths for the demo,
        // plus any paths set via set-route (stored in a JSON index).
        var indexJson = nginx.shared.get('route_index') || '[]';
        var paths;
        try { paths = JSON.parse(indexJson); } catch (e) { paths = []; }

        var routes = {};
        paths.forEach(function (p) {
            var b = nginx.shared.get(ROUTE_PREFIX + p);
            if (b) routes[p] = b;
        });

        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify(routes, null, 2) + '\n');
    };

    // Override set-route to also update the index
    setLoc.handler = function (r) {
        var path    = null;
        var backend = null;
        r.args.split('&').forEach(function (pair) {
            var kv = pair.split('=');
            if (kv[0] === 'path')    path    = decodeURIComponent(kv[1] || '');
            if (kv[0] === 'backend') backend = decodeURIComponent(kv[1] || '');
        });

        if (!path || !backend) {
            r.respond(400, {}, 'usage: /admin/set-route/?path=/api/&backend=v2\n');
            return;
        }

        nginx.shared.set(ROUTE_PREFIX + path, backend);

        // Update the index (best-effort; not atomic but fine for the demo)
        var indexJson = nginx.shared.get('route_index') || '[]';
        var paths;
        try { paths = JSON.parse(indexJson); } catch (e) { paths = []; }
        if (paths.indexOf(path) === -1) paths.push(path);
        nginx.shared.set('route_index', JSON.stringify(paths));

        r.respond(200, {}, 'route set: ' + path + ' → ' + backend + '\n');
    };
}());
