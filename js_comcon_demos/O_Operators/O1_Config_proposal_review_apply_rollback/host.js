// THE HOST — a tenant PROPOSES configuration; the operator reviews, applies, rolls back.
//
// The proposal is a QUOTATION: inert text over the tenant's own subtree, never
// run, holding no capability.  comcon.std.config.review() typechecks it against
// a policy (which subtree, which paths, which safety classes) and returns a
// plan with a content hash; diff() shows what would change with nothing
// applied; apply() is all-or-nothing and asks for explicit confirmation of
// guarded classes; rollback() restores the recorded previous values.

var locs = nginx.http.servers[0].locations;
var acme = locs.find(function (l) { return l.path === "/acme"; });

// WHAT THE TENANT SENDS: ordinary config-shaped sentences over ITS subtree.
var PROPOSAL = "acme.root('/srv/acme');"
             + "acme.proxy.connectTimeout(2500);"
             + "acme.proxy.pass('http://acme_backend');";

// WHAT THE OPERATOR ALLOWS: a subtree, a path allow-list, the classes that
// may be applied without being named one by one.
var POLICY = { type: "NginxLocation", root: "acme",
               allow: ["root", "alias", "proxy.*"], allowClass: ["safe"] };

// Well-typed, and still refused by nginx itself: the upstream does not exist.
// This is why apply() must be all-or-nothing.
var BAD = "acme.root('/srv/other');" + "acme.proxy.pass('http://no_such_upstream');";

// The other quotation shape: a cap-free FUNCTION proposal the operator
// realizes under an environment of the operator's choosing.
var Q = comcon.quote("function(a){ return a.n * 2; }");
var QH = comcon.quote("function(a){ return secretHost.token; }");

var last = null;
function snapshot() {
    return { root: acme.root, connectTimeout: acme.proxy.connectTimeout, pass: acme.proxy.pass };
}
function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }

locs.find(function (l) { return l.path === "/ctl"; }).handler = function (req) {
    var op = String(req.args || "").replace(/^op=/, ""), r = { op: op };
    try {
        if (op === "review") {
            var plan = comcon.std.config.review(PROPOSAL, POLICY);
            r.ok = plan.ok; r.hash = plan.hash;
            r.ops = plan.ops.map(function (o) { return o.path + " = " + o.verdict + " (" + o.cls + ")"; });
            r.refused = plan.refused.map(function (o) { return o.path + ": " + o.why; });
            r.now = snapshot();
        } else if (op === "diff") {
            var p2 = comcon.std.config.review(PROPOSAL, POLICY);
            r.diff = comcon.std.config.diff(p2, acme).map(function (d) {
                return d.path + ": " + JSON.stringify(d.from) + " -> " + JSON.stringify(d.to); });
            r.now = snapshot();                       // nothing applied yet
        } else if (op === "apply-noconfirm") {
            var p3 = comcon.std.config.review(PROPOSAL, POLICY);
            try { comcon.std.config.apply(p3, acme); r.out = "APPLIED"; }
            catch (e) { r.out = /class guarded/.test(e.message) ? "needs-confirm" : e.message; }
            r.now = snapshot();
        } else if (op === "apply") {
            var p4 = comcon.std.config.review(PROPOSAL, POLICY);
            last = comcon.std.config.apply(p4, acme, { confirm: ["acme.proxy.pass"] });
            r.applied = last.applied; r.hash = last.hash;
            r.now = snapshot();
        } else if (op === "rollback") {
            r.restored = comcon.std.config.rollback(last, acme);
            r.now = snapshot();
        } else if (op === "atomic") {
            var pb = comcon.std.config.review(BAD, POLICY);
            r.reviewOk = pb.ok;
            try { comcon.std.config.apply(pb, acme, { confirm: ["acme.proxy.pass"] }); r.out = "APPLIED"; }
            catch (e) { r.out = "refused: " + e.message; }
            r.now = snapshot();                       // untouched: the first op was undone
        } else if (op === "realize") {
            r.quoted = typeof Q + (Object.isFrozen(Q) ? ", frozen" : "");
            r.realized = comcon.realize(Q, { imports: [] }, comcon.env())({ n: 21 });
            try { comcon.realize(QH, { imports: [] }, comcon.env()); r.hidden = "admitted"; }
            catch (e) { r.hidden = "refused: " + (e.code || e.message); }
        } else {
            r.usage = "?op=review|diff|apply-noconfirm|apply|rollback|atomic|realize";
        }
    } catch (e) { r.error = String(e.message || e); }
    respond(req, r);
};

nginx.log(6, "O1: /ctl reviews, diffs, applies and rolls back a tenant's config proposal over /acme");
