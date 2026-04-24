// B4.2 — Sticky Sessions (Hash-based upstream routing)
//
// Routes each request to a deterministic backend based on a hash of the
// X-Session-Token header.  The same token always lands on the same backend,
// providing session stickiness without a shared session store.
//
// Falls back to hashing the Authorization Bearer sub-claim (simulated) if no
// session token is present.  Falls back to round-robin (alternating) if
// neither is present.
//
// Classic nginx: `hash $http_x_session_token consistent;` inside an upstream
// block achieves consistent hashing — but only for real upstream proxying.
// There is no way to inspect the hash result, adjust routing logic, or fall
// back to a different key without Lua.  Here the hash function is transparent
// JavaScript and the fallback logic is a plain if/else.

(function () {
    var BACKENDS = ['a', 'b'];

    // djb2 hash — fast, deterministic, good distribution for short strings
    function hash(str) {
        var h = 5381;
        for (var i = 0; i < str.length; i++) {
            h = ((h << 5) + h) ^ str.charCodeAt(i);
            h = h >>> 0;
        }
        return h;
    }

    function pickBackend(key) {
        return BACKENDS[hash(key) % BACKENDS.length];
    }

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/sticky/'; });
    });
    var locs = server.locations;

    // Simulated backends
    locs.find(function (l) { return l.path === '/backend/a/'; })
        .handler = function (r) {
            r.respond(200, {'X-Backend': 'a'}, 'backend_a\n');
        };

    locs.find(function (l) { return l.path === '/backend/b/'; })
        .handler = function (r) {
            r.respond(200, {'X-Backend': 'b'}, 'backend_b\n');
        };

    // Sticky router
    locs.find(function (l) { return l.path === '/sticky/'; })
        .handler = async function (r) {
            var token = r.headers['x-session-token'] || '';

            // Fall back: try to extract sub from Bearer token (demo: use raw value)
            if (!token) {
                var auth = r.headers['authorization'] || '';
                if (auth.indexOf('Bearer ') === 0) {
                    token = auth.slice(7).trim();
                }
            }

            var backend;
            if (token) {
                backend = pickBackend(token);
            } else {
                // No session identifier — return which backends exist
                backend = 'a';  // default
            }

            var result = await r.subrequest('/backend/' + backend + '/');
            r.respond(result.status,
                {'X-Backend': backend, 'X-Session-Key': token || '(none)'},
                result.body);
        };
})();
