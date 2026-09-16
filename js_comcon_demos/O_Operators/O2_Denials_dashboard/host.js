// THE HOST — the operator's view: every gate, every code, one switch.
//
// Two axes an operator needs kept apart:
//   DENIAL   codes -- a gate that fired while a fragment was RUNNING
//                     (nginx.tenantDenials().byOp, counted per worker)
//   REFUSAL  codes -- why a fragment was never admitted at all
//                     (e.code on the thrown error; comcon.refusalCodes() lists them)
// And one fleet-level switch, comcon.mode(): 'enforce' denies, 'audit' logs
// and ALLOWS (so a gate can be watched before it bites), 'learn' harvests.

var sock = nginx.createSocket("127.0.0.1:8259");

// A capability whose gate always fires: leased for one second at include time.
var probe = comcon.include(
    "function(a){ var v = s.port; return (v === undefined) ? 'denied' : 'allowed (' + v + ')'; }",
    { imports: [], grants: { s: comcon.mediate(sock, comcon.ttl(1)) } });

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }

at("/dash").handler = function (req) {
    var d = nginx.tenantDenials(), fired = {}, k;
    for (k in d.byOp) { if (d.byOp[k] > 0) { fired[k] = d.byOp[k]; } }
    respond(req, {
        mode:          d.mode,
        total:         d.total,
        fired:         fired,
        denialCodes:   Object.keys(d.byOp),
        refusalCodes:  comcon.refusalCodes(),
        worker:        nginx.workerId
    });
};

// ?set=enforce|audit|learn -- the fleet posture for THIS worker.
at("/mode").handler = function (req) {
    var m = String(req.args || "").replace(/^set=/, ""), r = { requested: m };
    try { comcon.mode(m); r.mode = nginx.tenantDenials().mode; }
    catch (e) { r.refused = String(e.message || e); r.mode = nginx.tenantDenials().mode; }
    respond(req, r);
};

// The same probe, under whatever the fleet is in.
at("/probe").handler = function (req) {
    var before = nginx.tenantDenials().byOp["cap.expired"];
    var r = probe({});
    respond(req, { mode: nginx.tenantDenials().mode, result: r,
                   counted: nginx.tenantDenials().byOp["cap.expired"] - before });
};

nginx.log(6, "O2: /dash, /mode?set=..., /probe");
