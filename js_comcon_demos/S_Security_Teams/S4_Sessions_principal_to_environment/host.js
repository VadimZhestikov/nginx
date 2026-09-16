// THE HOST — an authenticated principal becomes an environment.
//
// COMCON does not authenticate.  YOU verify who is calling (an mTLS subject,
// a JWT you checked); std.sessions maps that principal to a NARROWING of an
// environment you hold.  A mapping is data (never a capability), it can only
// narrow, and unknown / revoked / expired all resolve to the same thing: an
// empty environment.

var sock = nginx.createSocket("127.0.0.1:8257");

// The operator's environment: the root of the chain, built from what host JS
// holds at config time.  Nothing mints authority here.
function operatorEnv() {
    var e = comcon.env();
    comcon.grant(e, "s",    comcon.mediate(sock, comcon.allow(["address", "port"])));
    comcon.grant(e, "JSON", JSON);
    return e;
}

// The registry lives in nginx.shared (every worker resolves the same rows),
// which exists once the workers run -- so it is opened, and seeded once, at
// request time.
function registry() {
    var S = comcon.std.sessions({ sessions: nginx.shared });
    if (!nginx.shared.get("s4:seeded")) {
        S.grant("ci@acme",     { imports: ["s", "JSON"] });      // may hold the socket wrapper
        S.grant("dev@acme",    { imports: ["JSON"] });           // may not
        S.grant("greedy@acme", { imports: ["JSON", "nginx"] });  // names what the operator env does not grant
        nginx.shared.set("s4:seeded", 1);
    }
    return S;
}

// The same probe, bound under whatever the session resolved to.
var PROBE = "function(a){ return { sock: typeof s, json: typeof JSON }; }";

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }

// ?who=<principal>  -- in production `who` comes from something you verified,
// never from a client-supplied string (that would hand the client the session).
at("/session").handler = function (req) {
    var who = String(req.args || "").replace(/^who=/, "");
    var o = { who: who }, S = registry();
    try {
        var sess = S.resolve(who, operatorEnv());
        o.granted = sess.granted;
        o.reason  = sess.reason || null;
        // bind the probe under the resolved environment: a capability the
        // session carries is a grant; an intrinsic it carries (JSON) is an
        // import; a name it does not carry is neither -- the probe may still
        // mention it (imports) and finds nothing there.
        var grants = {}, k;
        for (k in sess.env.grants) { if (k !== "JSON") { grants[k] = sess.env.grants[k]; } }
        var f = comcon.include(PROBE, { imports: ["s", "JSON"], grants: grants });
        o.probe = f({});
    } catch (e) { o.refused = String(e.message || e); }
    o.registry = S.list();
    respond(req, o);
};

at("/revoke").handler = function (req) {
    var who = String(req.args || "").replace(/^who=/, ""), S = registry();
    S.revoke(who);
    respond(req, { revoked: who, registry: S.list() });
};

nginx.log(6, "S4: sessions for ci@acme, dev@acme, greedy@acme");
