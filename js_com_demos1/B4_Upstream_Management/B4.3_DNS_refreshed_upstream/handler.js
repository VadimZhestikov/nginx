// B4.3 — DNS-refreshed upstream addresses
//
// A SharedWorker daemon holds the most-recently resolved IP addresses for a
// set of hostnames.  The /dns-status/ endpoint shows the current resolution.
// The /admin/refresh/ endpoint triggers a new resolution cycle.
//
// Classic nginx: upstream addresses are fixed at startup (or reload).
// Changing an upstream IP requires a reload signal.  Automatic re-resolution
// of short-TTL DNS records is only supported in nginx Plus via `resolver` +
// `resolve` upstream parameter.  With a SharedWorker the resolution daemon
// runs inside the nginx process and can update addresses at any frequency
// without a reload.

(function () {
    var swPath = nginx.cycle.prefix + 'sw.js';

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/dns-status/'; });
    });
    var locs = server.locations;

    function swRequest(data) {
        return new Promise(function (resolve) {
            var localSw = new SharedWorker(swPath);
            localSw.onmessage = function (msg) { resolve(msg.data); };
            localSw.postMessage(data);
        });
    }

    // GET /dns-status/ — return current resolved IPs
    locs.find(function (l) { return l.path === '/dns-status/'; })
        .handler = async function (r) {
            var result = await swRequest({cmd: 'status'});
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(result, null, 2) + '\n');
        };

    // POST /admin/refresh/ — trigger a DNS re-resolution cycle
    locs.find(function (l) { return l.path === '/admin/refresh/'; })
        .handler = async function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            var result = await swRequest({cmd: 'refresh'});
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(result, null, 2) + '\n');
        };
})();
