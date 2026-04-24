// B1.2 — Route on Request Body (GraphQL-style operation routing)
//
// Classic nginx can only route on URI, method, and headers.  It cannot
// inspect the request body at all — that would require Lua or an upstream
// application to re-dispatch.
//
// Here JavaScript reads the POST body, parses the JSON "operation" field, and
// internally subrequests the correct handler — all inside nginx, with no
// external process.

(function () {
    var servers = nginx.http.servers;

    var server = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/graphql/'; });
    });

    var locs = server.locations;

    // Internal handlers
    locs.find(function (l) { return l.path === '/internal/users/'; })
        .handler = function (r) {
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify({handler: 'users handler', ok: true}) + '\n');
        };

    locs.find(function (l) { return l.path === '/internal/orders/'; })
        .handler = function (r) {
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify({handler: 'orders handler', ok: true}) + '\n');
        };

    // GraphQL dispatcher — read body, parse operation, route
    locs.find(function (l) { return l.path === '/graphql/'; })
        .handler = async function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var body = await r.readBody();

            var parsed;
            try {
                parsed = JSON.parse(body);
            } catch (e) {
                r.respond(400, {}, 'Invalid JSON: ' + e.message + '\n');
                return;
            }

            var operation = parsed.operation || '';

            var target;
            if (operation === 'GetUser' || operation === 'ListUsers') {
                target = '/internal/users/';
            } else if (operation === 'CreateOrder' || operation === 'GetOrder') {
                target = '/internal/orders/';
            } else {
                r.respond(400, {}, 'Unknown operation: ' + operation + '\n');
                return;
            }

            var result = await r.subrequest(target);
            r.respond(result.status, result.headers, result.body);
        };
})();
