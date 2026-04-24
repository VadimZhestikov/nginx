// D4.1 — Protocol-Aware Stream Routing (HTTP simulation)
//
// PRODUCTION CONCEPT:
//   In a real L4 deployment this would use the nginx stream{} module with
//   js_preread to inspect the first few bytes of the TCP payload, detect
//   the application protocol (SSH magic bytes 'SSH-2.0', TLS ClientHello,
//   HTTP/1.x GET/POST, etc.), and proxy_pass to the appropriate backend.
//
// THIS DEMO:
//   Simulates the same routing logic at the HTTP layer using an
//   X-Protocol header to represent the first-bytes detection result.
//   The routing logic is identical to what would run in the stream module
//   — only the protocol-sniffing input changes.
//
// Routing table:
//   X-Protocol: ssh     → /internal/ssh-backend/
//   X-Protocol: https   → /internal/https-backend/
//   X-Protocol: http    → /internal/http-backend/
//   (anything else)     → /internal/unknown/
//
// A SharedArrayBuffer accumulates per-protocol counters so /status/ can
// show routing statistics without any external store.

// SAB layout: [ssh_count, https_count, http_count, unknown_count]
var routeSab = new SharedArrayBuffer(16);
var routeArr = new Int32Array(routeSab);

(function () {
    var server = nginx.http.servers[0];

    // Protocol → internal location mapping
    var ROUTES = {
        'ssh':   '/internal/ssh-backend/',
        'https': '/internal/https-backend/',
        'http':  '/internal/http-backend/'
    };

    // SAB slot index per protocol
    var SLOTS = { 'ssh': 0, 'https': 1, 'http': 2, 'unknown': 3 };

    // ── Internal backends ────────────────────────────────────────────
    server.findLocation('/internal/ssh-backend/').handler = function (r) {
        r.respond(200, {
            'X-Backend': 'ssh'
        }, 'SSH backend — connection accepted (OpenSSH 8.9 compatible)\n');
    };

    server.findLocation('/internal/https-backend/').handler = function (r) {
        r.respond(200, {
            'X-Backend': 'https'
        }, 'HTTPS backend — TLS handshake complete, serving secure content\n');
    };

    server.findLocation('/internal/http-backend/').handler = function (r) {
        r.respond(200, {
            'X-Backend': 'http'
        }, 'HTTP backend — plaintext request served\n');
    };

    server.findLocation('/internal/unknown/').handler = function (r) {
        r.respond(400, {
            'X-Backend': 'unknown'
        }, 'Unknown protocol — connection rejected\n');
    };

    // ── /connect/ — protocol detection and routing ───────────────────
    server.findLocation('/connect/').handler = async function (r) {
        var proto = (r.headers['x-protocol'] || '').toLowerCase().trim();

        var target = ROUTES[proto] || '/internal/unknown/';
        var slot   = SLOTS[proto]  !== undefined ? SLOTS[proto] : SLOTS['unknown'];

        // Increment per-protocol counter
        Atomics.add(routeArr, slot, 1);

        nginx.log(4, 'Protocol routing: proto=' + (proto || 'none') + ' → ' + target);

        var res = await r.subrequest(target);
        r.respond(res.status, res.headers, res.body);
    };

    // ── /status/ — routing statistics ───────────────────────────────
    server.findLocation('/status/').handler = function (r) {
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            routing_table: Object.keys(ROUTES).map(function (proto) {
                return { protocol: proto, backend: ROUTES[proto] };
            }),
            stats: {
                ssh:     Atomics.load(routeArr, 0),
                https:   Atomics.load(routeArr, 1),
                http:    Atomics.load(routeArr, 2),
                unknown: Atomics.load(routeArr, 3)
            }
        }, null, 2) + '\n');
    };

})();
