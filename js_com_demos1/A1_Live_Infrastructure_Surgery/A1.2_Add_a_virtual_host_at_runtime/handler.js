// A1.2 — Add a Virtual Host at Runtime
//
// Demonstrates nginx.http.addServer(name) which creates a new virtual server
// reachable via the Host: header — no config file edit, no reload required.
//
// Two static servers must exist on the same listen address so that nginx
// builds a vhost-dispatch hash at start-up. addServer() then extends that
// hash to include the new name.  rebuildVhostDispatch() activates the change.

(function () {
    var http    = nginx.http;
    var servers = http.servers;

    // Helper: find a server by server_name
    function findSrv(name) {
        return servers.find(function (s) { return s.name === name; });
    }

    // Wire up static server 2 (/ping/)
    findSrv('static2.local')
        .locations.find(function (l) { return l.path === '/ping/'; })
        .handler = function (r) {
            r.respond(200, {}, 'static2-pong\n');
        };

    var adminSrv = findSrv('static1.local');

    // POST /add/?name=<hostname> — create a new virtual server
    adminSrv.locations
        .find(function (l) { return l.path === '/add/'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var name = r.args || 'dynamic.host';

            // Check if it already exists
            if (findSrv(name)) {
                r.respond(200, {}, 'already-exists: ' + name + '\n');
                return;
            }

            // addServer returns a new NginxServer object
            var newSrv = http.addServer(name);

            // Add a content handler to its root location
            newSrv.addLocation('/api/').handler = function (req) {
                req.respond(200, {}, 'Hello from dynamic server: ' + name + '\n');
            };

            newSrv.addLocation('/health/').handler = function (req) {
                req.respond(200, {}, 'ok\n');
            };

            // Activate the new vhost in the dispatch table
            http.rebuildVhostDispatch();

            nginx.log(4, 'Dynamic vhost added: ' + name);
            r.respond(200, {}, 'created: ' + name + '\n');
        };

    // POST /remove/?name=<hostname> — remove a previously added vhost
    adminSrv.locations
        .find(function (l) { return l.path === '/remove/'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var name = r.args || '';
            var removed = http.removeServer(name);
            if (removed) {
                http.rebuildVhostDispatch();
                nginx.log(4, 'Dynamic vhost removed: ' + name);
                r.respond(200, {}, 'removed: ' + name + '\n');
            } else {
                r.respond(404, {}, 'not-found: ' + name + '\n');
            }
        };

    // GET /list/ — list all server names
    adminSrv.locations
        .find(function (l) { return l.path === '/list/'; })
        .handler = function (r) {
            var names = servers.map(function (s) { return s.name; });
            r.respond(200, {}, names.join('\n') + '\n');
        };
})();
