// THE HOST — the cage is derived from observed behaviour, then enforced.
//
// A tenant's pricing handler is bound at /price.  Nobody wants to read it;
// everybody wants to know that the version shipped next week answers what
// this one answers.  So the operator RECORDS: every (input, output) the
// binding serves becomes a case.  The cases become a `tests` quotation --
// the admission phase that runs inside the compartment -- and `guard` pins
// it to the binding.  A rebind that answers a recorded case differently is
// refused before it is installed; one that answers the same goes live.
// Coverage says which of the handler's functions the traffic never entered,
// so the suite's silence about them is known, not assumed.

var V1 =
    "function(a){"
  + "  function base(sku){ return sku === 'widget' ? 250 : (sku === 'gadget' ? 900 : 100); }"
  + "  function bulk(qty){ return qty >= 10 ? 0.9 : 1; }"
  + "  function refund(a){ return { sku: a.sku, refund: -base(a.sku) * a.qty }; }"
  + "  if (a.qty < 0) { return refund(a); }"
  + "  return { sku: a.sku, qty: a.qty, total: base(a.sku) * a.qty * bulk(a.qty) };"
  + "}";
// next week's version: a rewrite that answers the same...
var V2_SAME = V1.replace("qty >= 10 ? 0.9 : 1", "qty < 10 ? 1 : 0.9");
// ...and one that quietly changes the bulk threshold
var V2_DRIFT = V1.replace("qty >= 10", "qty >= 5");

var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }
function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }

var price = at("/price");
function site(callable, epoch) {
    price.handler = callable === null
        ? function (req) { req.respond(410, { "content-type": "text/plain" }, "gone"); }
        : function (req) {
              var m = {}; String(req.args || "").split("&").forEach(function (kv) {
                  var p = kv.split("="); if (p[0]) { m[p[0]] = p[1] || ""; } });
              var r = callable({ sku: m.sku || "widget", qty: parseInt(m.qty || "1", 10) });
              r.epoch = epoch;
              respond(req, r);
          };
}

var q1 = comcon.quote(V1);
var h = comcon.bindAt(site, q1, { imports: [] });
var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode, bindings: true });
ops.register("pricing", h, q1);

// a candidate can be rehearsed on the host before it is bound: the same
// replay the admission phase will run, with nothing installed
var drift = comcon.include(V2_DRIFT, { imports: [] });

at("/ops").handler = function (req) {
    var op = String(req.args || "").replace(/^op=/, ""), r = {};
    try {
        if (op === "record")        r = ops.record("pricing");
        else if (op === "suite")    { var s = ops.suite("pricing");
                                      r = { recorded: s.recorded, distinct: s.distinct, unstable: s.unstable.length,
                                            cases: s.cases.map(function (c) { return c.input + " -> " + c.output; }) }; }
        else if (op === "coverage") { var c = ops.coverage("pricing");
                                      r = { functions: c.functions, tier: c.tier, exact: c.exact }; }
        else if (op === "rehearse") { var k = comcon.std.suite.check(drift, ops.suite("pricing"));
                                      r = { ok: k.ok, passed: k.passed, failed: k.failed }; }
        else if (op === "guard")    r = ops.guard("pricing");
        else if (op === "diff")     { var d = ops.diff("pricing", { imports: [] });
                                      r = { verdict: d.verdict, changes: d.changes }; }
        else if (op === "rebind-bad")  { try { ops.rebind("pricing", comcon.quote(V2_DRIFT)); r.epoch = "ADMITTED"; }
                                         catch (e) { r = { refused: e.code, epoch: h.epoch(), reason: e.message.replace(/^.*allow-suite/, "allow-suite") }; } }
        else if (op === "rebind-good") r = { epoch: ops.rebind("pricing", comcon.quote(V2_SAME)) };
        else r = { error: "unknown op" };
    } catch (e) { r = { error: e.name + ": " + e.message }; }
    respond(req, r);
};
