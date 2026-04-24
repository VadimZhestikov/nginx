// A3.3 — Config Dry Run (Location Match Simulator)
//
// nginx.http.match(uri [, serverName]) simulates nginx's location-matching
// algorithm and returns the location object that would handle the URI.
//
// This enables:
//   - Config validation without sending real traffic
//   - Debugging "which location handles /foo/bar?"
//   - Writing unit tests for routing rules in CI
//   - Building admin UIs that show routing at a glance

(function () {
    var http  = nginx.http;
    var srv   = http.servers[0];
    var locs  = srv.locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Wire up simple handlers for the production locations.
    // Note: loc.path returns the URI path WITHOUT the match modifier.
    // For "location = /health", loc.path === '/health' (not '= /health').
    // For "location ^~ /static/", loc.path === '/static/'.
    findLoc('/health').handler = function (r) {
        r.respond(200, {}, 'healthy\n');
    };

    findLoc('/static/').handler = function (r) {
        r.respond(200, {}, 'static file: ' + r.uri + '\n');
    };

    findLoc('/api/v1/').handler = function (r) {
        r.respond(200, {}, 'API v1\n');
    };

    findLoc('/api/v2/').handler = function (r) {
        r.respond(200, {}, 'API v2\n');
    };

    findLoc('/api/').handler = function (r) {
        r.respond(200, {}, 'API (generic)\n');
    };

    findLoc('/admin/').handler = function (r) {
        r.respond(200, {}, 'admin area\n');
    };

    findLoc('/').handler = function (r) {
        r.respond(200, {}, 'root handler: ' + r.uri + '\n');
    };

    // GET /match/?<uri> — return which location would match the given URI
    // Optional: ?server=<name> to test against a specific server
    findLoc('/match/').handler = function (r) {
        // Parse query: ?uri=/some/path or ?/some/path (simple form)
        var query = r.args;
        var uri, serverName;

        // Support ?uri=/path&server=name or just ?/path
        if (query.indexOf('uri=') === 0) {
            var parts = query.split('&');
            uri = decodeURIComponent(parts[0].replace('uri=', ''));
            for (var i = 1; i < parts.length; i++) {
                if (parts[i].indexOf('server=') === 0) {
                    serverName = parts[i].replace('server=', '');
                }
            }
        } else if (query) {
            uri = query.charAt(0) === '/' ? query : '/' + query;
        }

        if (!uri) {
            r.respond(400, {}, 'usage: /match/?<uri>  or  /match/?uri=<uri>&server=<name>\n');
            return;
        }

        var matched = serverName
            ? http.match(uri, serverName)
            : http.match(uri);

        if (!matched) {
            r.respond(200, {}, JSON.stringify({ uri: uri, matched: null, reason: 'no match' }) + '\n');
            return;
        }

        // matched.pattern includes the modifier (e.g. "= /health", "^~ /static/")
        // matched.path is the bare URI path without modifier (e.g. "/health", "/static/")
        r.respond(200, {}, JSON.stringify({
            uri:     uri,
            matched: matched.pattern,
            path:    matched.path,
            type:    matched.type || 'prefix',
        }) + '\n');
    };
})();
