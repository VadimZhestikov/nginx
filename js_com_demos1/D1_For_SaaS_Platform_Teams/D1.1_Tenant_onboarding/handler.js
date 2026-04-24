// D1.1 — Tenant Onboarding
//
// SaaS platform use-case: a single admin API call provisions a full
// virtual host for a new tenant in under 100 ms — no config file edit,
// no nginx reload, no downtime for existing tenants.
//
// How it works:
//   POST /admin/tenants/   { "name": "acme-corp" }
//     → nginx.http.addServer("acme-corp")
//     → newSrv.addLocation("/api/") with a tenant-aware handler
//     → http.rebuildVhostDispatch() makes it live immediately
//
//   GET /status/  → lists all virtual hosts currently known to nginx

(function () {
    var http = nginx.http;

    // ----------------------------------------------------------------
    // Wire up the anchor server
    // ----------------------------------------------------------------
    var anchorSrv = http.servers.find(function (s) {
        return s.name === 'anchor.local';
    });
    anchorSrv.findLocation('/ping/').handler = function (r) {
        r.respond(200, {}, 'anchor-pong\n');
    };

    // ----------------------------------------------------------------
    // Admin server
    // ----------------------------------------------------------------
    var adminSrv = http.servers.find(function (s) {
        return s.name === 'admin.local';
    });

    // POST /admin/tenants/
    // Body: JSON with { "name": "tenant-name", "port": 8175 }
    // port is optional (ignored in this demo — tenants share port 8175)
    adminSrv.findLocation('/admin/tenants/').handler = async function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'Method Not Allowed\n');
            return;
        }

        var body = await r.readBody();
        var data;
        try {
            data = JSON.parse(body);
        } catch (e) {
            r.respond(400, {}, 'Invalid JSON body\n');
            return;
        }

        var name = (data.name || '').trim();
        if (!name) {
            r.respond(400, {}, 'Missing "name" field\n');
            return;
        }

        // Check if a server with this name already exists
        var existing = http.servers.find(function (s) { return s.name === name; });
        if (existing) {
            r.respond(409, {}, JSON.stringify({
                status: 'conflict',
                message: 'Tenant already exists: ' + name
            }) + '\n');
            return;
        }

        // Create the virtual host
        var tenantSrv = http.addServer(name);

        // Add a content handler for the tenant's /api/ location
        tenantSrv.addLocation('/api/').handler = function (req) {
            req.respond(200, {
                'Content-Type': 'application/json'
            }, JSON.stringify({
                tenant: name,
                message: 'Welcome to ' + name + ' API',
                status: 'active'
            }) + '\n');
        };

        // Add a health check location
        tenantSrv.addLocation('/health/').handler = function (req) {
            req.respond(200, {}, 'ok tenant=' + name + '\n');
        };

        // Make it visible to the request router
        http.rebuildVhostDispatch();

        nginx.log(4, 'Tenant provisioned: ' + name);

        r.respond(201, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            status: 'created',
            tenant: name,
            api: 'http://127.0.0.1:8175/api/ (Host: ' + name + ')'
        }) + '\n');
    };

    // GET /status/ — list all virtual hosts
    adminSrv.findLocation('/status/').handler = function (r) {
        var servers = http.servers.map(function (s) {
            return {
                name: s.name,
                locations: s.locations.map(function (l) { return l.path; })
            };
        });
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            server_count: servers.length,
            servers: servers
        }, null, 2) + '\n');
    };
})();
