// COMCON dogfood — the HOST program (full authority, HOST_ROOT).
//
// It sets the stage the confined tenant runs in, then gets out of the way:
//   1. creates + activates a listener socket,
//   2. GRANTS that socket into the tenant (so we can prove, on live traffic,
//      that a tenant holding a real capability still cannot walk its reach),
//   3. wires /denials to report the tenant's denial counters — the host side
//      of the audit -> enforce loop.

var sock = nginx.createSocket("127.0.0.1:8219");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);

// A2.1: DECLARE "granted" in the onboarding delta.  This records a name for
// nginx.tenantLearning(); it does not confer the socket -- conferring is
// comcon.grant(env, name, cap) / comcon.include(src, {grants}).  The second
// argument is accepted and ignored, kept here as it was written.
nginx.grantToTenant("granted", sock);

// A4: expose the denial report on a host-owned location.
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/denials") {
        locs[i].handler = function (req) {
            req.respond(200, { "content-type": "application/json" },
                        JSON.stringify(nginx.tenantDenials()));
        };
    }
}

nginx.log(6, "COMCON dogfood: host ready (granted socket + /denials)");
