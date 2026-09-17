// THE HOST — one replace, four workers, no torn state.
//
// comcon.bindShared(key, quotation, contract, onRequest) is bindAt's
// multi-worker spelling.  The current {epoch, source} lives in nginx.shared
// (lock-free, visible to every worker); each worker's handler RECONCILES
// lazily -- on a request it reads the shared epoch and, if newer than what it
// compiled, recompiles the shared source in ITS OWN compartment and swaps.  So a
// replace() executed in whichever worker took the control request fans out to
// all of them, each on its next request.  Only source text ever crosses.

var V1 = "function(a){ return { version: 'v1', greeting: 'hello' }; }";
var V2 = "function(a){ return { version: 'v2', greeting: 'hello, patched' }; }";

function onRequest(req, callable, epoch) {
    var hdr = { "content-type": "application/json",
                "x-epoch": String(epoch), "x-worker": String(nginx.workerIdx) };
    if (callable === null) { req.respond(410, hdr, "gone\n"); return; }
    req.respond(200, hdr, JSON.stringify(callable({})) + "\n");
}

var h = comcon.bindShared("greeting", comcon.quote(V1), { imports: [] }, onRequest);

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/g"; }).handler = h.handler;

locs.find(function (l) { return l.path === "/ctl"; }).handler = function (req) {
    var op = String(req.args || "").replace(/^op=/, ""), r = { op: op, onWorker: nginx.workerIdx };
    try {
        if      (op === "patch")  { r.epoch = h.replace(comcon.quote(V2)); }
        else if (op === "remove") { r.epoch = h.remove(); }
        else if (op === "revive") { r.epoch = h.revive(); }
        else                      { r.epoch = h.epoch(); }
    } catch (e) { r.error = String(e.message || e); }
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(r));
};

nginx.log(6, "L2: shared binding 'greeting' on 4 workers; /ctl?op=patch|remove|revive|epoch");
