// A2.2 — Cross-Worker Broadcast via nginx.shared
//
// nginx.shared is a cross-worker key-value store backed by a fixed shared-
// memory zone.  Writes from any worker are immediately visible to all other
// workers.
//
// Pattern demonstrated:
//   1. Admin writes a config value to nginx.shared (e.g. upstream URL, theme).
//   2. Every worker reads from nginx.shared on each request — always sees the
//      latest value regardless of which worker handled the update.
//
// For immediate (non-polling) propagation, combine nginx.shared writes with
// nginx.broadcast() so each worker picks up the new value synchronously.

(function () {
    var locs = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Seed initial config values once per worker startup.
    // nginx.shared is only available in worker context (after fork), so we
    // use nginx.broadcast() to run initialisation in each worker.
    nginx.broadcast(function () {
        // Only seed if not already set (first worker wins)
        if (!nginx.shared.get('upstream.url')) {
            nginx.shared.set('upstream.url',  'http://backend-v1.internal/');
            nginx.shared.set('feature.theme', 'light');
            nginx.shared.set('max.rps',       '1000');
        }
    });

    // GET /config/ — return all config KVs visible to this worker
    findLoc('/config/').handler = function (r) {
        var keys  = nginx.shared.keys().sort();
        var lines = keys.map(function (k) {
            return k + '=' + (nginx.shared.get(k) || '');
        });
        r.respond(200, {}, lines.join('\n') + '\n');
    };

    // POST /admin/update/?key=value — write a config entry
    // Body format: key=value  (application/x-www-form-urlencoded style)
    findLoc('/admin/update/').handler = async function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

        var body  = await r.readBody();
        var parts = body.split('=');
        if (parts.length < 2) { r.respond(400, {}, 'bad format: expected key=value\n'); return; }

        var key   = parts[0].trim();
        var value = parts.slice(1).join('=').trim();

        nginx.shared.set(key, value);
        nginx.log(4, 'Config updated: ' + key + '=' + value);

        r.respond(200, {}, 'updated: ' + key + '=' + value + '\n');
    };

    // POST /admin/delete/?key — remove a config entry
    findLoc('/admin/delete/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

        var key     = r.args || '';
        var deleted = nginx.shared.delete(key);
        r.respond(200, {}, deleted ? 'deleted: ' + key + '\n' : 'not-found: ' + key + '\n');
    };

    // GET /worker-info/ — show which worker is handling this request
    findLoc('/worker-info/').handler = function (r) {
        r.respond(200, {}, 'worker=' + nginx.workerIdx + '\n');
    };
})();
