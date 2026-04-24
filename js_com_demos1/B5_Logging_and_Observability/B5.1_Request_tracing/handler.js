// B5.1 — Request Tracing (X-Request-Id propagation)
//
// Guarantees that every request has an X-Request-Id:
//   - If the incoming request already carries X-Request-Id, it is propagated
//     through to the response (client or upstream generated the id).
//   - If the header is absent, a new UUID-like id is generated and attached.
//
// The id is available in the handler via r.ctx.requestId and is added to the
// response via addResponseHook.
//
// Classic nginx: $request_id is a built-in variable added in nginx 1.11.0
// that generates a random hex string.  However:
//   - It cannot reuse an incoming X-Request-Id
//   - It cannot be formatted as a UUID
//   - The response header must be set with add_header, which has quirky
//     inheritance rules in nested location blocks
//
// The JS hooks give full control over both request and response phases.

(function () {
    // Simple pseudo-UUID generator (not cryptographically random)
    function generateId() {
        function hex(n) {
            return ('00000000' + (Math.random() * 0xffffffff >>> 0).toString(16)).slice(-8);
        }
        return hex() + '-' + hex().slice(0,4) + '-4' + hex().slice(0,3) +
               '-' + (8 + (Math.random() * 4 | 0)).toString(16) + hex().slice(0,3) +
               '-' + hex() + hex().slice(0,4);
    }

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/api/'; });
    });

    var loc = server.locations.find(function (l) { return l.path === '/api/'; });

    // Request hook: assign/propagate X-Request-Id
    loc.addHook(function (r, next) {
        var incoming = r.headers['x-request-id'];
        r.ctx.requestId = incoming || generateId();
        next(r);
    });

    // Response hook: add X-Request-Id to the response headers
    loc.addResponseHook(function (r) {
        r.setHeader('x-request-id', r.ctx.requestId);
    });

    // Handler
    loc.handler = function (r) {
        r.respond(200, {}, 'OK — request-id: ' + r.ctx.requestId + '\n');
    };
})();
