// D4.2 — Dynamic Port Listener
//
// PRODUCTION CONCEPT:
//   A full implementation would call nginx.createSocket(port) to open a
//   new TCP listener at runtime — useful for dynamic port allocation in
//   gaming servers, voice/video conferencing, or multi-tenant services
//   where each tenant gets a dedicated port.
//
// THIS DEMO:
//   Shows the closest achievable equivalent with the current COM API:
//   nginx.http.addServer(hostname) creates a new virtual host on the
//   same shared listen port (8191), demonstrating the dynamic provisioning
//   pattern that a createSocket() call would extend to port-level isolation.
//
//   Each "dynamic listener" gets:
//     - Its own virtual server identified by server_name
//     - A /data/ handler that returns the service metadata
//     - A /health/ endpoint for readiness probes
//     - Registration tracked in nginx.shared for cross-worker visibility
//
// POST /admin/listen/  { "name": "svc-alpha", "service": "game-server" }
// GET  /status/        → list all registered services

(function () {
    var http = nginx.http;

    // Track registered services in nginx.shared (cross-worker)
    var SVCKEY = 'services:';

    function getServices() {
        var keys = nginx.shared.keys().filter(function (k) {
            return k.indexOf(SVCKEY) === 0;
        });
        return keys.map(function (k) {
            try { return JSON.parse(nginx.shared.get(k)); }
            catch (e) { return null; }
        }).filter(Boolean);
    }

    // ── Wire up anchor server ────────────────────────────────────────
    http.servers.find(function (s) {
        return s.name === 'anchor.local';
    }).findLocation('/ping/').handler = function (r) {
        r.respond(200, {}, 'anchor-pong\n');
    };

    // ── Admin server ─────────────────────────────────────────────────
    var adminSrv = http.servers.find(function (s) {
        return s.name === 'admin.local';
    });

    // POST /admin/listen/ — register a new virtual service
    adminSrv.findLocation('/admin/listen/').handler = async function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'Method Not Allowed\n');
            return;
        }

        var body = await r.readBody();
        var data;
        try { data = JSON.parse(body); } catch (e) {
            r.respond(400, {}, 'Invalid JSON\n');
            return;
        }

        var name    = (data.name    || '').trim();
        var service = (data.service || 'generic').trim();

        if (!name) {
            r.respond(400, {}, 'Missing "name" field\n');
            return;
        }

        // Check if already registered
        if (nginx.shared.get(SVCKEY + name)) {
            r.respond(409, {}, 'Service already registered: ' + name + '\n');
            return;
        }

        // Create the virtual server
        var svcSrv = http.addServer(name);

        // /data/ — returns service metadata
        svcSrv.addLocation('/data/').handler = function (req) {
            req.respond(200, {
                'Content-Type': 'application/json'
            }, JSON.stringify({
                service: name,
                type: service,
                status: 'active',
                // In a real createSocket() scenario, this would be the
                // dynamically allocated port number
                virtual_host: name,
                listen_port: 8191
            }) + '\n');
        };

        // /health/ — readiness probe
        svcSrv.addLocation('/health/').handler = function (req) {
            req.respond(200, {}, 'ok service=' + name + '\n');
        };

        http.rebuildVhostDispatch();

        // Register in shared store
        var meta = { name: name, service: service, registered: Date.now() };
        nginx.shared.set(SVCKEY + name, JSON.stringify(meta));

        nginx.log(4, 'Dynamic listener registered: ' + name + ' (' + service + ')');

        r.respond(201, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            status: 'registered',
            service: name,
            type: service,
            note: 'Access via Host: ' + name + ' header on port 8191'
        }) + '\n');
    };

    // GET /status/ — list all registered services
    adminSrv.findLocation('/status/').handler = function (r) {
        var services = getServices();
        var servers  = http.servers.map(function (s) { return s.name; });

        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            registered_services: services.length,
            services: services,
            all_servers: servers
        }, null, 2) + '\n');
    };

})();
