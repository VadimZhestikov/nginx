// THE HOST — the same fragment on two tiers, the same answers, the same gates.
//
// On the compiled build (objs_jit) a fragment is lowered to C by maxim at
// INCLUDE time; on the interpreter build it is not.  Nothing in this file
// changes between the two: the tier is a property of the binary.  The demo's
// test runs both binaries and compares the answers -- the SR-2 faithfulness
// gate (compiled == interpreted) in miniature.

// Class B: a token check -- split, an FNV hash over every code unit, hex.
var token = comcon.include(
    "function(req){"
  + "  var parts = String(req.token).split('.'), h = 2166136261 >>> 0, i, j, p;"
  + "  for (i = 0; i < parts.length; i++) {"
  + "    p = parts[i];"
  + "    for (j = 0; j < p.length; j++) { h = Math.imul(h ^ p.charCodeAt(j), 16777619) >>> 0; }"
  + "  }"
  + "  return { parts: parts.length, hash: h.toString(16), ok: parts.length === 3 && (h & 1) === 1 }; }",
    { imports: ["String", "Math"] });

// Class A: a byte scan with an int accumulator (the shape M5.1a lowered).
var scan = comcon.include(
  "function(req){ var s = String(req.text), n = s.length, i, acc = 0, bad = 0;"
  + "  for (i = 0; i < n; i++) { var c = s.charCodeAt(i); acc = (acc * 31 + c) | 0; if (c < 32 || c > 126) { bad++; } }"
  + "  return { len: n, acc: acc >>> 0, bad: bad }; }",
    { imports: ["String"] });

// The gate: a runaway loop with a 100 ms deadline.  The compiled tier polls
// on back-edges (gas), so the abort fires inside lowered C too.
var spin = comcon.include("function(req){ var x = 0; while (true) { x = (x + 1) | 0; } }",
                          { imports: [], meter: comcon.meter({ timeoutMs: 100 }) });

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
var locs = nginx.http.servers[0].locations;

locs.find(function (l) { return l.path === "/run"; }).handler = function (req) {
    var t = String(req.args || "").replace(/^token=/, "") || "eyJhbGci.eyJzdWIi.SflKxwRJ";
    var a = { token: token({ token: t }), scan: scan({ text: t + "\tend" }) };
    var t0 = Date.now(), gate;
    try { spin({}); gate = "RETURNED"; }
    catch (e) { gate = /interrupted/.test(String(e.message)) ? "interrupted" : String(e.message); }
    respond(req, {
        tier:   { token: comcon.aotStatus(token), scan: comcon.aotStatus(scan), spin: comcon.aotStatus(spin) },
        answer: a,
        gate:   { spin: gate, ms: Date.now() - t0 }
    });
};

nginx.log(6, "D2: token check + byte scan + a metered spin; the tier is the binary's");
