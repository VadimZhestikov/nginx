// THE HOST — one capability, six attenuations, one probe.
//
// The probe text never changes; only the MEDIATION around the socket does.
// So every difference in the answers is a difference in the membrane, and
// every denial is a stable code an auditor can pin (nginx.tenantDenials().byOp).
var sock = nginx.createSocket("127.0.0.1:8254");

var PROBE = "function(a){ return {"
          + "  address:  typeof s.address,"
          + "  port:     typeof s.port,"
          + "  fd:       typeof s.fd,"
          + "  listener: (s.listener === null) ? 'null' : typeof s.listener"
          + " }; }";

function arm(cap) { return comcon.include(PROBE, { imports: [], grants: { s: cap } }); }

var arms = {
    // the raw wrapper: every scalar field readable; the reach edge (listener)
    // is null cross-compartment -- the entry to the config tree is closed
    raw:     arm(sock),
    // allow: ONLY the listed fields exist
    allow:   arm(comcon.mediate(sock, comcon.allow(["port"]))),
    // redact: the listed fields are hidden, the rest stay
    redact:  arm(comcon.mediate(sock, comcon.redact(["address"]))),
    // ttl: a lifetime; the clock starts when the capability crosses (include time)
    leased:  arm(comcon.mediate(sock, comcon.ttl(1))),
    // a stack: allow, then a lifetime, then a budget -- attenuations only ever narrow
    stacked: arm(comcon.mediate(comcon.mediate(comcon.mediate(sock,
                 comcon.allow(["address", "port"])), comcon.ttl(3600)), comcon.uses("s1-stack", 100, 60)))
};
// uses: a fleet-wide budget of 2 exercises per 60 s, keyed by name.  EVERY
// gated read is an exercise (a read of a field is a use as much as a call),
// so this probe reads one field per call: two calls spend the budget.
var metered = comcon.include("function(a){ return typeof s.address; }",
    { imports: [], grants: { s: comcon.mediate(sock, comcon.uses("s1-demo", 2, 60)) } });
// revoke: the grant is withheld entirely -- `s` does not exist inside
var revoked = comcon.include("function(a){ return { s: typeof s }; }",
                             { grants: { s: comcon.mediate(sock, comcon.revoke()) } });

function counts() {
    var d = nginx.tenantDenials().byOp, c = {}, k;
    for (k in d) { if (d[k] > 0) { c[k] = d[k]; } }
    return c;
}

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/arms"; }).handler = function (req) {
    var o = { before: counts() }, k;
    for (k in arms) { o[k] = arms[k]({}); }
    o.revoked = revoked({});
    // the budget of 2: the third exercise is denied (budget.uses), the field reads undefined
    o.metered = [metered({}), metered({}), metered({})];
    o.after = counts();
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(o));
};

nginx.log(6, "S1: six attenuations of one socket capability, one probe");
