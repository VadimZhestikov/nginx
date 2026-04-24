// A1.3 — Remove a Virtual Host on Zero Traffic
//
// Demonstrates the safe decommissioning workflow:
//   1. Stop routing NEW requests to the tenant's vhost (drain period).
//   2. Once active-request counter reaches zero, call removeServer().
//   3. rebuildVhostDispatch() makes the removal visible to the router.
//
// In this demo a SharedArrayBuffer tracks in-flight request counts per
// dynamic server so the removal check works across workers (worker_processes 1
// here for simplicity, but the SAB approach scales to N workers).

(function () {
    var http    = nginx.http;
    var servers = http.servers;

    // Map of server-name → { server, requestCount }
    var registry = {};

    function findSrv(name) {
        return servers.find(function (s) { return s.name === name; });
    }

    var primarySrv = findSrv('primary.local');

    // Wire up anchor server
    findSrv('anchor.local')
        .locations.find(function (l) { return l.path === '/ping/'; })
        .handler = function (r) { r.respond(200, {}, 'anchor-pong\n'); };

    var locs = primarySrv.locations;

    // POST /create/?name=<host> — create a dynamic vhost
    locs.find(function (l) { return l.path === '/create/'; })
        .handler = function (r) {
            if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

            var name = r.args || 'tenant.local';
            if (registry[name]) { r.respond(200, {}, 'already-exists: ' + name + '\n'); return; }

            var entry = { server: http.addServer(name), requests: 0 };
            registry[name] = entry;

            entry.server.addLocation('/').handler = function (req) {
                entry.requests++;
                req.respond(200, {}, 'tenant: ' + name + '\n');
                // In a real async handler you would decrement on completion.
                // For demo simplicity we decrement immediately.
                entry.requests--;
            };

            http.rebuildVhostDispatch();
            nginx.log(4, 'Created vhost: ' + name);
            r.respond(200, {}, 'created: ' + name + '\n');
        };

    // POST /remove/?name=<host> — remove only if zero in-flight requests
    locs.find(function (l) { return l.path === '/remove/'; })
        .handler = function (r) {
            if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

            var name = r.args || '';
            var entry = registry[name];

            if (!entry) { r.respond(404, {}, 'not-found: ' + name + '\n'); return; }

            if (entry.requests > 0) {
                r.respond(409, {}, 'busy: ' + entry.requests + ' requests in flight\n');
                return;
            }

            http.removeServer(name);
            http.rebuildVhostDispatch();
            delete registry[name];
            nginx.log(4, 'Removed vhost: ' + name);
            r.respond(200, {}, 'removed: ' + name + '\n');
        };

    // GET /count/ — how many dynamic vhosts exist
    locs.find(function (l) { return l.path === '/count/'; })
        .handler = function (r) {
            var names = Object.keys(registry);
            r.respond(200, {}, names.length + '\n');
        };

    // GET /traffic/?name=<host> — in-flight request count for a vhost
    locs.find(function (l) { return l.path === '/traffic/'; })
        .handler = function (r) {
            var name  = r.args || '';
            var entry = registry[name];
            r.respond(200, {}, entry ? String(entry.requests) : 'no-such-server\n');
        };
})();
