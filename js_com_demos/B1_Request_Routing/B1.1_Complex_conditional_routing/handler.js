// B1.1 — Complex Conditional Routing
//
// Routes a request based on THREE simultaneous conditions:
//   1. X-Tenant request header
//   2. URI prefix (r.uri)
//   3. Accept request header
//
// Classic nginx can match URI with location blocks and a handful of variables,
// but combining three independent header conditions into branching logic
// requires an awkward chain of map{} + if{} directives.  Here it is a single
// plain JavaScript if/else statement.

(function () {
    var servers = nginx.http.servers;

    var server = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/route/'; });
    });

    var locs = server.locations;

    // Internal backend simulations — just respond with a label
    locs.find(function (l) { return l.path === '/internal/json/'; })
        .handler = function (r) {
            var tenant = r.ctx.tenant || 'unknown';
            r.respond(200,
                {'Content-Type': 'application/json'},
                JSON.stringify({backend: 'acme-json', tenant: tenant}) + '\n');
        };

    locs.find(function (l) { return l.path === '/internal/html/'; })
        .handler = function (r) {
            r.respond(200,
                {'Content-Type': 'text/html'},
                '<html><body>acme default backend</body></html>\n');
        };

    // Main routing logic
    locs.find(function (l) { return l.path === '/route/'; })
        .handler = async function (r) {
            var tenant = (r.headers['x-tenant'] || '').toLowerCase();
            var accept = (r.headers['accept'] || '').toLowerCase();
            // X-Original-Uri simulates what a real front-end proxy would forward;
            // falls back to the request URI itself for direct access.
            var uri    = r.headers['x-original-uri'] || r.uri;

            if (tenant === 'acme' &&
                uri.indexOf('/api/') === 0 &&
                accept.indexOf('json') !== -1) {

                // Route to acme JSON backend
                r.ctx.tenant = 'acme';
                var result = await r.subrequest('/internal/json/');
                r.respond(result.status, result.headers, result.body);

            } else if (tenant === 'acme') {

                // Route to acme HTML backend
                var result = await r.subrequest('/internal/html/');
                r.respond(result.status, result.headers, result.body);

            } else {

                // Default backend
                r.respond(200, {}, 'default backend\n');
            }
        };
})();
