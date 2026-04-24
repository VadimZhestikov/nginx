// B2.2 — Dynamic API Key Blocklist
//
// A SharedWorker thread maintains the blocklist in its own heap.
// Worker request handlers check keys via postMessage, allowing the blocklist
// to be updated at runtime without reloading nginx.
//
// Classic nginx: API key validation requires lua-resty-redis or a bespoke
// auth_request microservice.  Changes take effect only after the external
// store is updated AND the cache (if any) expires.  Here the SW updates
// its in-memory Set instantly and all workers see the new state on their
// very next request.

(function () {
    var swPath = nginx.cycle.prefix + 'sw.js';
    var sw = new SharedWorker(swPath);

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/api/'; });
    });
    var locs = server.locations;

    // Helper: ask the SW to check/revoke via Promise
    function swRequest(data) {
        return new Promise(function (resolve) {
            var localSw = new SharedWorker(swPath);
            localSw.onmessage = function (msg) { resolve(msg.data); };
            localSw.postMessage(data);
        });
    }

    // GET /api/ — check X-API-Key header
    locs.find(function (l) { return l.path === '/api/'; })
        .handler = async function (r) {
            var key = r.headers['x-api-key'] || '';
            if (!key) {
                r.respond(401, {}, 'Missing X-API-Key header\n');
                return;
            }

            var result = await swRequest({cmd: 'check', key: key});

            if (!result.allowed) {
                r.respond(403, {}, 'Forbidden: key revoked\n');
                return;
            }

            r.respond(200, {}, 'OK: key accepted\n');
        };

    function parseArgs(qs) {
        var out = {};
        (qs || '').split('&').forEach(function (part) {
            var kv = part.split('=');
            if (kv[0]) out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || '');
        });
        return out;
    }

    // POST /admin/revoke/?key=<key> — revoke an API key
    locs.find(function (l) { return l.path === '/admin/revoke/'; })
        .handler = async function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var key = parseArgs(r.args)['key'] || '';
            if (!key) {
                r.respond(400, {}, 'Missing ?key= query parameter\n');
                return;
            }

            var result = await swRequest({cmd: 'revoke', key: key});
            r.respond(200, {}, 'Revoked: ' + result.key + '\n');
        };
})();
