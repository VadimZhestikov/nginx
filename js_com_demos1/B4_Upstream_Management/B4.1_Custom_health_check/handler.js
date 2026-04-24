// B4.1 — Custom Health Check
//
// A SharedWorker thread maintains the health state for a set of upstream peers.
// The /status/ endpoint queries the SW and reports current health.
// The /admin/toggle/ endpoint marks a peer up or down (simulating what a real
// health-check loop would do after an HTTP probe fails or recovers).
//
// Classic nginx: the open-source nginx has no built-in active health checks for
// HTTP upstreams (that feature is in nginx Plus).  The community workaround is
// the third-party ngx_upstream_check_module or writing a health-check daemon
// outside nginx.  Here the SharedWorker IS the health-check daemon — running
// inside the nginx process, zero extra infrastructure.

(function () {
    var swPath = nginx.cycle.prefix + 'sw.js';

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/status/'; });
    });
    var locs = server.locations;

    function swRequest(data) {
        return new Promise(function (resolve) {
            var localSw = new SharedWorker(swPath);
            localSw.onmessage = function (msg) { resolve(msg.data); };
            localSw.postMessage(data);
        });
    }

    // Simulated backend health endpoint
    locs.find(function (l) { return l.path === '/_health/'; })
        .handler = function (r) {
            r.respond(200, {}, 'OK\n');
        };

    // GET /status/ — return health state from the SW
    locs.find(function (l) { return l.path === '/status/'; })
        .handler = async function (r) {
            var result = await swRequest({cmd: 'status'});
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(result, null, 2) + '\n');
        };

    function parseArgs(qs) {
        var out = {};
        (qs || '').split('&').forEach(function (part) {
            var kv = part.split('=');
            if (kv[0]) out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || '');
        });
        return out;
    }

    // POST /admin/toggle/?addr=127.0.0.1:8142&healthy=false
    locs.find(function (l) { return l.path === '/admin/toggle/'; })
        .handler = async function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var args    = parseArgs(r.args);
            var addr    = args['addr']    || '';
            var healthy = args['healthy'] !== 'false';

            if (!addr) {
                r.respond(400, {}, 'Missing ?addr= parameter\n');
                return;
            }

            await swRequest({cmd: 'set', addr: addr, healthy: healthy});
            r.respond(200, {}, 'Updated ' + addr + ' → ' +
                (healthy ? 'UP' : 'DOWN') + '\n');
        };
})();
