// THE HOST — patch at noon, native by 12:01, on every worker.
//
// L1 and L2 showed a live replace: new text admitted at request time, fanned
// out to every worker, no reload.  What they could not show was the tier:
// a worker cannot compile (the gcc thread does not survive fork()), so a
// live epoch stayed interpreted until the next reload -- and aotStatus()
// said so rather than pretend.
//
// Now the worker that admits an epoch sends its wrapper text to the master,
// the master spawns one short-lived helper that compiles it exactly as at
// config load (compile-only: nothing runs there, not even an IIFE source),
// and every worker adopts the artifacts on its next request, verified
// (codegen version, no process-bound calls, the atom table rebound to its
// own runtime).  The interpreted epoch serves meanwhile; the answer is the
// same on both tiers.

var V1 =
    "function(a){"
  + "  function weight(i){ return (i * 7) % 5 + 1; }"
  + "  function score(n){ var i, s = 0; for (i = 0; i < n; i++) { s += weight(i) * (i % 3); } return s; }"
  + "  return { score: score(a.n), n: a.n };"
  + "}";
var V2 = V1.replace("(i % 3)", "(i % 4)");

var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }
function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }

function onReq(req, callable, epoch) {
    if (callable === null) { req.respond(410, { "content-type": "text/plain" }, "gone"); return; }
    var a = comcon.aotStatus(callable), r = callable({ n: 200 });
    r.epoch = epoch;
    r.worker = nginx.workerIdx;
    r.tier = a.compiled > 0 ? "native" : "bytecode";
    r.compiled = a.compiled + "/" + a.functions;
    r.via = a.via;
    r.pending = a.pending;
    respond(req, r);
}

var h = comcon.bindShared("score", comcon.quote(V1), { imports: [] }, onReq);
at("/score").handler = h.handler;

at("/ops").handler = function (req) {
    var op = String(req.args || "").replace(/^op=/, ""), r = {};
    try {
        if (op === "replace") r = { epoch: h.replace(comcon.quote(V2)) };
        else r = { error: "unknown op" };
    } catch (e) { r = { error: e.name + ": " + e.message }; }
    respond(req, r);
};
