// THE HOST — three bad tenants and one good one, on the same worker.
//
// Every fragment runs under three bounds, each a word in `meter`:
//   timeoutMs      a wall-clock deadline (5 s if you say nothing; never unbounded)
//   memoryBytes    what one INVOCATION may allocate (a burst; 16 MB default)
//   retainedBytes  what the fragment may HOLD across calls (a leak; 8 MB default)
// The three bad tenants below hit one bound each.  The good one keeps being
// served, because a bound is a refusal charged to ONE fragment, not a worker
// that dies.

// A runaway loop, allowed 100 ms.  The abort is uncatchable INSIDE the
// fragment (a tenant cannot catch its own deadline) and arrives here as an
// ordinary exception.
var spinner = comcon.include(
    "function(req){ var x = 0; while (true) { x = (x + 1) | 0; } }",
    { imports: [], meter: comcon.meter({ timeoutMs: 100 }) });

// A burst allocator, allowed 1 MB per call.  The compartment survives the
// refusal; the next call starts from a clean allowance.
var burster = comcon.include(
    "function(req){ var a = []; for (;;) { a.push(new Array(4096).fill(0)); } }",
    { imports: ["Array"], meter: comcon.meter({ memoryBytes: 1048576 }) });

// A leaker: keeps 64 KB per call, allowed to RETAIN 1 MB.  Each invocation is
// charged with what it left behind; past the cap the fragment is refused at
// its next call (E_MEM_RETAINED) until its epoch is replaced.
var leaker = comcon.include(
    "(function(){ var keep = []; return function(req){ keep.push('x'.repeat(65536)); return keep.length; }; })()",
    { imports: [], meter: comcon.meter({ retainedBytes: 1048576 }) });

// The neighbour that did nothing wrong.
var good = comcon.include("function(req){ return 'still served'; }", { imports: [] });

// A fragment's failure arrives as an ordinary host exception whose message is
// "comcon: fragment: <name>: <text> at <comcon-fragment>:line:col"; keep the
// middle for the reader.
function outcome(fn) {
    try { return { returned: fn() }; }
    catch (e) {
        var m = String(e.message || e).replace(/^comcon: fragment: /, "").replace(/ at <comcon-fragment>.*$/, "");
        return { threw: m, code: e.code || null };
    }
}

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/budgets"; }).handler = function (req) {
    var o = {};

    var t0 = Date.now();
    o.spin = outcome(function () { return spinner({}); });
    o.spin.ms = Date.now() - t0;

    o.burst = outcome(function () { return burster({}); });
    o.burstAgain = outcome(function () { return burster({}); });   // same answer, clean slate

    var served = 0, refused = 0, lastCode = null;
    for (var i = 0; i < 40; i++) {
        var r = outcome(function () { return leaker({}); });
        if (r.returned !== undefined) { served++; } else { refused++; lastCode = r.code; }
    }
    o.leak = { served: served, refused: refused, code: lastCode,
               status: comcon.memStatus(leaker) };      // {retained, invocations, refused, cap}

    o.good = outcome(function () { return good({}); });
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(o));
};

nginx.log(6, "P3: spinner (100 ms), burster (1 MB/call), leaker (1 MB retained), and a good neighbour");
