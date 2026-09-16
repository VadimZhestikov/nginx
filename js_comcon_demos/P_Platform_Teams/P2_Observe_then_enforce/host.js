// THE HOST — roll a policy out observe-first, per BINDING, not per fleet.
//
// comcon.mode('audit') would shadow every tenant on the worker at once, which
// is the wrong granularity: shadowing one tenant's NEW policy must not stop
// enforcing every other tenant's.  So the posture is a word in the contract
// (`onViolation`), and it wins in both directions against the fleet mode.
import tenant from "./tenant.js";

// ONBOARDING: the fleet starts in LEARN mode, and says so before the first
// include -- the confined compartment is created at the first include, and
// learn mode is what installs the harvesting surface in it.
comcon.mode("learn");

// One capability: a socket, leased for ONE second.  `ttl` starts its clock
// when the capability crosses into the compartment (include time), so a
// second after nginx starts, every read through it is a `cap.expired`
// violation -- the always-denying gate that makes the posture visible.
var sock = nginx.createSocket("127.0.0.1:8251");
function leased() { return comcon.mediate(sock, comcon.ttl(1)); }

// The same text, two postures.  A THIRD binding says nothing and inherits
// whatever the fleet is in (enforce, unless someone switched it).
var shadow  = comcon.include(String(tenant), { imports: [], grants: { s: leased() }, onViolation: "audit" });
var enforce = comcon.include(String(tenant), { imports: [], grants: { s: leased() }, onViolation: "deny" });
var inherit = comcon.include(String(tenant), { imports: [], grants: { s: leased() } });

// A tenant written against MORE than it was given is admitted in learn mode,
// and its reaches are harvested (which names, how many hits) rather than
// fatal.  That record is what you turn into a contract -- and the two
// bindings above with their own posture word keep that word regardless of
// what the fleet is in.
var greedy = comcon.include(
    "function(req){ nginx.http.addServer({}); createSocket('127.0.0.1:9'); fetch('http://x/');"
  + " return 'handled ' + req.uri; }");

function respond(req, o) {
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(o));
}
var locs = nginx.http.servers[0].locations;
function at(path) { return locs.find(function (l) { return l.path === path; }); }

at("/postures").handler = function (req) {
    respond(req, {
        fleet:   nginx.tenantDenials().mode,
        shadow:  shadow({}),        // audit: violations logged, reads ALLOWED -- whatever the fleet is in
        enforce: enforce({}),       // deny:  reads denied                  -- whatever the fleet is in
        inherit: inherit({}),       // no word: follows the fleet
        denials: nginx.tenantDenials().byOp
    });
};

at("/learn").handler = function (req) {
    var r; try { r = greedy({ uri: req.uri }); } catch (e) { r = "threw: " + e.message; }
    respond(req, { result: r, learning: nginx.tenantLearning() });
};

// When the environment is settled: flip the fleet.  ?set=enforce | audit | learn
at("/mode").handler = function (req) {
    var m = String(req.args || "").replace(/^set=/, "");
    comcon.mode(m);
    respond(req, { fleet: nginx.tenantDenials().mode });
};

nginx.log(6, "P2: fleet in LEARN; three bindings of one tenant (audit / deny / inherit); a greedy tenant harvested");
