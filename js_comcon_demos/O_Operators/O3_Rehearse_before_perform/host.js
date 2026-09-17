// THE HOST — a policy change rehearses before it performs.
//
// Three library verbs, no new authority:
//   std.policy.diff(current, candidate)  narrowing?  widening?  incomparable?
//   onViolation: "audit" on ONE binding  the candidate shadowed on live traffic
//   ops.wouldDeny(binding)               what enforce WOULD have refused, per binding
// Then the flip: the same text, onViolation: "deny".
import tenant from "./tenant.js";

var sock = nginx.createSocket("127.0.0.1:8266");

var CURRENT = { imports: [], grants: { s: comcon.mediate(sock, comcon.allow(["port"])) },
                meter: comcon.meter({ timeoutMs: 200 }), checkRequest: true };
// the candidate: a budget of TWO reads per hour on the same field, a one-hour
// lease, a tighter deadline.  (A mask is not a gate -- a redacted read is simply
// absent and never counted -- so the thing to rehearse is a gated word: the
// budget fires `budget.uses`, and under audit that firing is a would-deny row.)
var CANDIDATE = { imports: [], grants: { s: comcon.mediate(comcon.mediate(comcon.mediate(sock, comcon.allow(["port"])),
                                            comcon.uses("acme:port", 2, 3600)), comcon.ttl(3600)) },
                  meter: comcon.meter({ timeoutMs: 100 }), checkRequest: true };
// a candidate someone else proposed: the raw socket, and the request check off
var LOOSER = { imports: [], grants: { s: sock }, meter: comcon.meter({ timeoutMs: 500 }), checkRequest: false };

function withPosture(c, p) { var o = {}, k; for (k in c) { o[k] = c[k]; } o.onViolation = p; return o; }

var live    = comcon.include(String(tenant), withPosture(CURRENT, "deny"));
var shadow  = comcon.include(String(tenant), withPosture(CANDIDATE, "audit"));   // the rehearsal
var enforce = comcon.include(String(tenant), withPosture(CANDIDATE, "deny"));    // the performance

var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode });

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }

at("/diff").handler = function (req) {
    respond(req, { candidate: comcon.std.policy.diff(CURRENT, CANDIDATE),
                   looser:    comcon.std.policy.diff(CURRENT, LOOSER) });
};

// live traffic goes through BOTH the current binding and the shadowed candidate
at("/t").handler = function (req) {
    var a = live({}), b = shadow({});
    respond(req, { served: a, shadowed: b });
};

at("/rehearsal").handler = function (req) {
    respond(req, { live: ops.wouldDeny(live), candidate: ops.wouldDeny(shadow) });
};

at("/perform").handler = function (req) {
    respond(req, { enforced: enforce({}), denials: ops.wouldDeny(enforce) });
};

nginx.log(6, "O3: /diff, /t (live + shadow), /rehearsal, /perform");
