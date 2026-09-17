// THE HOST — a live binding is a sequence of EPOCHS; the fix is one more of them.
//
// comcon.bindAt(site, quotation, contract) admits a quotation and installs it at
// a site YOU define (a function that wires a callable into location.handler --
// the ordinary js_com setter, no parallel install path).  The handle it returns
// is the epoch machine: replace(q) admits the new text and swaps it in as epoch
// n+1 (the old one is retained), rollback() restores it, remove() tombstones
// the site (410), revive() brings it back.  Superseded fragments are freed, so
// replacing five hundred times leaves the heap where it was.

var locs   = nginx.http.servers[0].locations;
var target = locs.find(function (l) { return l.path === "/price"; });
var live   = null;            // the callable of the current epoch (for aotStatus)

// The SITE: how a callable becomes the handler.  null = the tombstone.
function site(callable, epoch) {
    live = callable;
    if (callable === null) {
        target.handler = function (req) {
            req.respond(410, { "content-type": "text/plain", "x-epoch": String(epoch) }, "gone\n");
        };
        return;
    }
    target.handler = function (req) {
        var qty = (String(req.args || "").replace(/^qty=/, "") | 0) || 1;
        var o = callable({ qty: qty });
        req.respond(200, { "content-type": "application/json", "x-epoch": String(epoch) },
                    JSON.stringify(o) + "\n");
    };
}

// Epoch 0: the pricing rule with the bug (a discount that never applies).
var V1 = "function(a){ var unit = 250; var disc = a.qty > 10 ? 0.1 : 0.0;"
       + "  return { version: 'v1', qty: a.qty, total: unit * a.qty, discount: disc }; }";
// The noon fix: the discount is applied.
var V2 = "function(a){ var unit = 250; var disc = a.qty > 10 ? 0.1 : 0.0;"
       + "  return { version: 'v2', qty: a.qty, total: (unit * a.qty * (1 - disc)) | 0, discount: disc }; }";

var h = comcon.bindAt(site, comcon.quote(V1), { imports: [] });

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
locs.find(function (l) { return l.path === "/ctl"; }).handler = function (req) {
    var op = String(req.args || "").replace(/^op=/, ""), r = { op: op };
    try {
        if      (op === "patch")    { r.epoch = h.replace(comcon.quote(V2)); }   // admitted under the binding's contract
        else if (op === "rollback") { r.epoch = h.rollback(); }
        else if (op === "remove")   { r.epoch = h.remove(); }
        else if (op === "revive")   { r.epoch = h.revive(); }
        else if (op === "describe") { r.describe = h.describe(); r.epoch = h.epoch(); }
        else if (op === "tier")     { r.epoch = h.epoch(); r.aot = live ? comcon.aotStatus(live) : null; }
        else if (op === "stress") {
            // 300 replacements; the heap must come back to where it was
            var before = nginx.jsMemUsage();
            for (var i = 0; i < 300; i++) { h.replace(comcon.quote("function(a){ return { v: " + (i % 7) + " }; }")); }
            nginx.gc();
            r.delta = nginx.jsMemUsage().mallocSize - before.mallocSize;
            r.epoch = h.epoch();
            h.replace(comcon.quote(V2));                            // back to the fix
        }
        else { r.epoch = h.epoch(); }
    } catch (e) { r.error = String(e.message || e); r.code = e.code || null; }
    respond(req, r);
};

nginx.log(6, "L1: /price served by epoch 0 (v1); /ctl?op=patch|rollback|remove|revive|describe|tier|stress");
