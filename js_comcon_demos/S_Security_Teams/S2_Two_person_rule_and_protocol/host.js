// THE HOST — who may act (cosign) and in what order (protocol).
//
// `cosign` is the one word in the vocabulary a holder cannot satisfy alone:
// the ATTEMPT is the consent, and the operation runs when a quorum of
// DISTINCT principals has attempted the same decision within a window.  `as`
// is written here, on the trusted side -- a fragment cannot choose who it is.
//
// `protocol` is a session type over a capability's own operations: a bare
// step happens exactly once, in place; a starred step any number of times;
// after the last step the conversation is over (a one-shot capability).

var seq = Date.now();                            // a fresh decision key per nginx start

function armed(principal) {
    var cap = nginx.outbound();
    var m = comcon.mediate(cap, comcon.allowHosts("https://*.example.com"));
    m = comcon.mediate(m, comcon.cosign({ key: "rotate-key-" + seq, quorum: 2, within: 900, as: principal }));
    return { cap: cap,
             act: comcon.include("function(a){ return out.request('https://api.example.com/rotate') === undefined ? 'denied' : 'recorded'; }",
                                 { imports: [], grants: { out: m } }) };
}
var alice = armed("alice"), bob = armed("bob");

var sock = nginx.createSocket("127.0.0.1:8255");
function conversation(steps) {
    var m = comcon.mediate(sock, comcon.allow(["address", "port", "fd"]));
    m = comcon.mediate(m, comcon.protocol.apply(null, steps));
    return comcon.include(
        "function(a){ var o = [], i; for (i = 0; i < a.ops.length; i++) {"
      + "  var v = s[a.ops[i]]; o.push(v === undefined ? '-' : a.ops[i]); } return o; }",
        { imports: [], grants: { s: m } });
}

function counts() {
    var d = nginx.tenantDenials().byOp, c = {}, k;
    for (k in d) { if (d[k] > 0) { c[k] = d[k]; } }
    return c;
}
function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }

// Each call is one operator's attempt.  The host reads the queue afterwards:
// an intent reaches it only once the quorum is met.
at("/attempt").handler = function (req) {
    var who = String(req.args || "").replace(/^who=/, "");
    var a = who === "bob" ? bob : alice;
    var r = a.act({});
    respond(req, { who: who === "bob" ? "bob" : "alice", result: r,
                   queued: a.cap.pending().requests.length, denials: counts() });
};

at("/protocol").handler = function (req) {
    respond(req, {
        inOrder:    conversation(["address", "port*", "fd"])({ ops: ["address", "port", "port", "fd"] }),
        skipStar:   conversation(["address", "port*", "fd"])({ ops: ["address", "fd"] }),
        outOfOrder: conversation(["address", "port*", "fd"])({ ops: ["fd", "address"] }),
        oneShot:    conversation(["fd"])({ ops: ["fd", "fd", "fd"] }),
        denials:    counts()
    });
};

nginx.log(6, "S2: cosign (alice + bob, quorum 2) and protocol (address, port*, fd)");
