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
    var server = nginx.http.servers.find(function (s) { return s.name === 'mirror-demo'; });
    var loc    = server.locations.find(function (l) { return l.path === '/'; });

    // content handler — produce a response so the response hook has one to decorate
    loc.handler = function (r) { r.respond(200, {}, 'mirror ok\n'); };

    mirror.attach(server, loc, {

        onClientAccept: function (ev) {
            var f = ev.flow;                  // the REAL per-connection object
            f.client     = ev.clientAddr;     // stash at accept
            f.acceptedAt = Date.now();
            f.reqCount   = 0;                 // per-connection request counter
            f.serial     = ev.table.incr('mirror:connSerial');  // unique per connection
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
            ev.setResponseHeader('x-mirror-route',       ev.ctx.route);
            ev.setResponseHeader('x-mirror-conn-reqs',   f.reqCount);          // proves flow-local persists
            ev.setResponseHeader('x-mirror-conn-serial', f.serial);            // same across keepalive = one conn object
            ev.setResponseHeader('x-mirror-client',      f.client || 'MISSING'); // proves accept linkage
            ev.setResponseHeader('x-mirror-total',       ev.table.get('mirror:total'));
            ev.setResponseHeader('x-mirror-closed',      ev.table.get('mirror:closed') || 0);
            if (ev.ctx.capError) {
                ev.setResponseHeader('x-mirror-cap-error', ev.ctx.capError);
            }
        },

        // fires when the connection closes (pilgrim conn.onClose) — flow-local
        // stashed at accept is still readable here.
        onClientClose: function (ev) {
            ev.table.incr('mirror:closed');
            nginx.log(5, 'mirror: onClientClose serial=' + ev.flow.serial +
                         ' reqs=' + ev.flow.reqCount +
                         ' client=' + ev.flow.client);
        }
    });

    // --- TLS rule: inspect the ClientHello (JA3 inputs) at handshake time ----
    // flow-local set in onClientHello (BEFORE the handshake completes) is read
    // back on the HTTP response — the spine now reaches down to TLS.
    var tls = nginx.http.servers.find(function (s) { return s.name === 'mirror-tls'; });
    if (tls) {
        var tloc = tls.locations.find(function (l) { return l.path === '/'; });
        tloc.handler = function (r) { r.respond(200, {}, 'mirror tls ok\n'); };

        mirror.attach(tls, tloc, {
            onClientHello: function (ev) {
                var f = ev.flow, ch = ev.clientHello;
                f.sni         = ch.sni || '';
                f.tlsVersion  = ch.version;
                f.cipherCount = (ch.cipherSuites || []).length;
                f.extCount    = (ch.extensions   || []).length;
                // JA3-style fingerprint string (MD5 left to a hashing lib):
                f.ja3 = [ch.version,
                         (ch.cipherSuites    || []).join('-'),
                         (ch.extensions      || []).join('-'),
                         (ch.supportedGroups || []).join('-'),
                         (ch.ecPointFormats  || []).join('-')].join(',');
            },
            onResponseHeaders: function (ev) {
                var f = ev.flow;
                ev.setResponseHeader('x-mirror-sni',          f.sni || 'none');
                ev.setResponseHeader('x-mirror-tls-version',  f.tlsVersion  || 0);
                ev.setResponseHeader('x-mirror-cipher-count', f.cipherCount || 0);
                ev.setResponseHeader('x-mirror-ext-count',    f.extCount    || 0);
                ev.setResponseHeader('x-mirror-ja3',          f.ja3 || '');
            }
        });
    }

    // --- LB rule: per-request pool selection (iRules `pool`) -----------------
    // No content handler here — proxy_pass owns the content phase; the rule just
    // picks the pool in onRequestHeaders via ev.selectUpstream().
    var lbloc = server.locations.find(function (l) { return l.path === '/lb/'; });
    if (lbloc) {
        mirror.attach(server, lbloc, {
            onRequestHeaders: function (ev) {
                ev.selectUpstream(ev.header('x-pool') === 'b'
                                  ? 'mirror_poolB' : 'mirror_poolA');
            }
        });
    }

    // --- peer-level LB (iRules LB::select): a custom balancer picks the node --
    // The request hook stashes a peer preference in flow-local; the upstream's
    // onSelectPeer balancer reads it. -1 falls back to round-robin.
    var pool = nginx.http.upstreams.find(function (u) { return u.name === 'mirror_pool'; });
    if (pool) {
        pool.onSelectPeer(function (peers, flow) {
            return (flow && typeof flow.peerIndex === 'number') ? flow.peerIndex : -1;
        });
    }
    var pploc = server.locations.find(function (l) { return l.path === '/lbpeer/'; });
    if (pploc) {
        mirror.attach(server, pploc, {
            onRequestHeaders: function (ev) {
                var h = ev.header('x-peer');
                ev.flow.peerIndex = (h === undefined) ? -1 : parseInt(h, 10);
            }
        });
    }

    // --- L4 rule (iRules CLIENT_DATA): inspect raw TCP bytes, detect protocol -
    var sstream = nginx.stream && nginx.stream.servers && nginx.stream.servers[0];
    if (sstream) {
        mirror.attachStream(sstream, {
            onClientData: function (ev) {
                var d     = ev.data || '';
                var proto = (d.indexOf('SSH-') === 0) ? 'ssh'
                          : (d.indexOf('GET ') === 0 || d.indexOf('POST ') === 0) ? 'http'
                          : (d.charCodeAt(0) === 22) ? 'tls'   // TLS handshake record (0x16)
                          : 'unknown';
                nginx.log(5, 'mirror onClientData: proto=' + proto +
                             ' from=' + ev.clientAddr + ' bytes=' + d.length);
                ev.finalize(200);
            }
        });
    }
})();
