// mirror example — one iRule translated by hand to the mirror event/command
// model (the phase-1 acceptance test).
//
// Original intent, iRules-style:
//   when CLIENT_ACCEPTED  { set client [IP::client_addr]; set n 0 }
//   when HTTP_REQUEST     { incr n; table incr mirror:total
//                           set route [expr {[HTTP::header X-Mirror-Route] eq "beta" ? "beta" : "stable"}] }
//   when HTTP_RESPONSE    { HTTP::header insert X-Mirror-* ... }
//
// It exercises the whole spine: state stashed at ACCEPT is read at RESPONSE
// (accept->request->response linkage), a per-CONNECTION counter persists across
// keepalive requests (flow-local), and a global counter spans connections
// (table). A capability self-test proves per-event command gating.

var mirror = globalThis.mirror;

(function () {
    var server = nginx.http.servers[0];
    var loc    = server.locations.find(function (l) { return l.path === '/'; });

    // content handler — produce a response so the response hook has one to decorate
    loc.handler = function (r) { r.respond(200, {}, 'mirror ok\n'); };

    mirror.attach(server, loc, {

        onClientAccept: function (ev) {
            var f = ev.flow;
            f.client     = ev.clientAddr;     // stash at accept
            f.acceptedAt = Date.now();
            f.reqCount   = 0;                 // per-connection request counter
        },

        onRequestHeaders: function (ev) {
            var f = ev.flow;
            f.reqCount = (f.reqCount || 0) + 1;               // per-connection
            ev.table.incr('mirror:total');                   // global across conns
            ev.ctx.route = (ev.header('x-mirror-route') === 'beta') ? 'beta' : 'stable';

            // capability self-test: setResponseHeader is NOT valid in a request
            // event — mirror must throw. Prove it, and carry the message forward.
            if (ev.header('x-mirror-captest')) {
                try { ev.setResponseHeader('x-nope', '1'); ev.ctx.capError = 'NOT-CAUGHT'; }
                catch (e) { ev.ctx.capError = e.message; }
            }
        },

        onResponseHeaders: function (ev) {
            var f = ev.flow;
            ev.setResponseHeader('x-mirror-route',     ev.ctx.route);
            ev.setResponseHeader('x-mirror-conn-reqs', f.reqCount);        // proves flow-local
            ev.setResponseHeader('x-mirror-client',    f.client || 'MISSING'); // proves accept linkage
            ev.setResponseHeader('x-mirror-total',     ev.table.get('mirror:total'));
            if (ev.ctx.capError) {
                ev.setResponseHeader('x-mirror-cap-error', ev.ctx.capError);
            }
        }
    });
})();
