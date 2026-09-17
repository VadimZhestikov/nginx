// THE HOST — the audit is a query over the program tree and the session's own resources.
//
// Three reads and one write, all library verbs over shipped operators:
//   comcon.pom(fn)        a frozen NodeView; reads return quotations; binding redacted
//   node.callsites(name)  from bytecode, with line numbers -- "where is fetch used?"
//   comcon.std.ops(...)   trustReport / bindings / denials -- from the resources handed in
//   comcon.harden(...)    rewrite the sites no grant can name, install as an epoch
import vendor from "./vendor.js";

var locs = nginx.http.servers[0].locations;
var target = locs.find(function (l) { return l.path === "/v"; });

// The vendor code as a LIVE binding, so the rewrite has somewhere to land.
var FRAG = "function (n) {"
         + "  var log = [];"
         + "  function real(s) { log.push('RAN:' + s); return s.toUpperCase(); }"
         + "  var g = real;"
         + "  var out = g('a') + g('b');"
         + "  return out + '|' + log.join(','); }";
function site(callable, epoch) {
    target.handler = function (req) {
        if (callable === null) { req.respond(410, { "content-type": "text/plain" }, "gone\n"); return; }
        req.respond(200, { "content-type": "text/plain", "x-epoch": String(epoch) }, String(callable(0)) + "\n");
    };
}
var q1 = comcon.quote(FRAG);
var h  = comcon.bindAt(site, q1, { imports: [] });

// A capability-holding tenant, so the denial counters have something to count.
var sock = nginx.createSocket("127.0.0.1:8265");
var reach = comcon.include("function(){ return (s.listener === null) ? 'denied' : 'LEAKED'; }",
                           { imports: [], grants: { s: sock } });   // the raw wrapper: the reach edge is the gate

// The session: verbs decompose over the resources passed in; nothing ambient.
var ops = comcon.std.ops({ log: nginx.tenantDenials, learn: nginx.tenantLearning,
                           mode: comcon.mode, bindings: true });
ops.register("vendor", h, q1);
var bare = comcon.std.ops();           // a session given nothing has only describe()

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
function at(p) { return locs.find(function (l) { return l.path === p; }); }

at("/report").handler = function (req) {
    reach({});                                             // one denial, so the report is not empty
    var tr = ops.trustReport();
    respond(req, {
        verbs:      Object.keys(ops).sort(),
        bareVerbs:  Object.keys(bare).sort(),
        bindings:   ops.bindings(),                        // [{name, epoch, tombstoned, snapshots}]
        trust:      { bindings: tr.bindings, enforcedBy: tr.enforcedBy },
        denials:    ops.denials(),
        withheld:   bare.describe().withheld,              // verbs no resource backs, with the reason
        noHost:     bare.describe().resources.filter(function (r) { return !r.host; }).map(function (r) { return r.name; }),
        absent:     comcon.std.describe().absent.map(function (a) { return a.name; })
    });
};

// ?name=fetch  -- where is it used, from bytecode, without running anything
at("/where").handler = function (req) {
    var name = String(req.args || "").replace(/^name=/, "") || "fetch";
    var node = comcon.pom(vendor);
    respond(req, {
        name:       name,
        references: node.references(name).length,
        callsites:  node.callsites(name).map(function (c) { return { line: c.line, method: c.method }; }),
        functions:  node.query("function").map(function (n) { return n.name; }),
        readsAreQuotations: Object.isFrozen(node.text()) && typeof node.text() === "object",
        bindingRedacted:    !("names" in node.binding)
    });
};

// ?op=harden | rollback -- rewrite the locally-bound call sites, install as an epoch
at("/rewrite").handler = function (req) {
    var op = String(req.args || "").replace(/^op=/, ""), r = { op: op };
    try {
        if (op === "harden") {
            // `$$` is the site's own source; the wrapper refuses the call and answers 'X'
            var rep = ops.rewrite("vendor", "call(g)",
                                  comcon.quote("(function (t) { return 'X'; })(function () { return $$; })"));
            r.sitesRewritten = rep.count; r.epoch = h.epoch();
        } else if (op === "rollback") { r.epoch = ops.rollback("vendor"); }
        else { r.epoch = h.epoch(); r.snapshot = ops.snapshot("vendor").source.slice(0, 40) + "…"; }
    } catch (e) { r.error = String(e.message || e); }
    respond(req, r);
};

nginx.log(6, "A1: /report, /where?name=fetch, /rewrite?op=harden|rollback, /v");
