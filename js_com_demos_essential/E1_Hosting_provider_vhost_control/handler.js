// E1 — Hosting-provider vhost control
//
// A hosting provider manages many customer sites on a shared nginx instance.
// Tenants must be enabled on boarding and disabled on suspension without any
// config-file edit or reload.
//
// Control-plane API (port 8201, Host: admin.internal):
//   POST /tenants/enable?<hostname>   — bring a tenant site online
//   POST /tenants/disable?<hostname>  — take it offline instantly
//   GET  /tenants/list                — list active hostnames
//
// Tenant site (port 8201, Host: <hostname>):
//   GET  /                            — tenant landing page
//   GET  /health                      — health check endpoint

(function () {
    var http    = nginx.http;
    var servers = http.servers;

    // Index of dynamically-added tenant hostnames for fast lookup.
    var tenants = {};

    function findSrv(name) {
        return servers.find(function (s) { return s.name === name; });
    }

    // Wire up the anchor server's health endpoint.
    findSrv('anchor.internal')
        .locations.find(function (l) { return l.path === '/health'; })
        .handler = function (r) { r.respond(200, {}, 'anchor-ok\n'); };

    var adminSrv = findSrv('admin.internal');

    // POST /tenants/enable?<hostname>
    adminSrv.locations
        .find(function (l) { return l.path === '/tenants/enable'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var host = r.args;
            if (!host) {
                r.respond(400, {}, 'hostname required as query string\n');
                return;
            }

            if (tenants[host]) {
                r.respond(200, {}, 'already-enabled: ' + host + '\n');
                return;
            }

            // Create the virtual server and install two locations.
            var srv = http.addServer(host);

            srv.addLocation('/').handler = function (req) {
                req.respond(200, {}, 'Welcome to ' + host + '\n');
            };

            srv.addLocation('/health').handler = function (req) {
                req.respond(200, {}, 'ok\n');
            };

            // Activate in the vhost-dispatch hash — one atomic call.
            http.rebuildVhostDispatch();

            tenants[host] = true;
            nginx.log(4, 'tenant enabled: ' + host);
            r.respond(200, {}, 'enabled: ' + host + '\n');
        };

    // POST /tenants/disable?<hostname>
    adminSrv.locations
        .find(function (l) { return l.path === '/tenants/disable'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var host = r.args;
            if (!host) {
                r.respond(400, {}, 'hostname required as query string\n');
                return;
            }

            if (!tenants[host]) {
                r.respond(404, {}, 'not-found: ' + host + '\n');
                return;
            }

            // Remove the virtual server and update the dispatch table.
            http.removeServer(host);
            http.rebuildVhostDispatch();

            delete tenants[host];
            nginx.log(4, 'tenant disabled: ' + host);
            r.respond(200, {}, 'disabled: ' + host + '\n');
        };

    // GET /tenants/list
    adminSrv.locations
        .find(function (l) { return l.path === '/tenants/list'; })
        .handler = function (r) {
            var active = Object.keys(tenants);
            r.respond(200, {}, active.length
                ? active.join('\n') + '\n'
                : '(no active tenants)\n');
        };
})();
