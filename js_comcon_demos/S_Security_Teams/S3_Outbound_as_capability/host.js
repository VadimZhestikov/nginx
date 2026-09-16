// THE HOST — reach outward is a capability, and the HOST performs the I/O.
//
// A confined fragment is invoked synchronously, so it cannot be handed a
// fetch.  Instead the host mints an outbound capability, narrows it by
// destination (`allowHosts`), and grants that.  The fragment RECORDS an
// intent; the glob is checked in the compartment, at the moment of the
// request; the host reads the queue afterwards and performs what survived.
// (The tenant proposes what it cannot apply -- the same shape as config.)

var cap = nginx.outbound();
var narrowed = comcon.mediate(cap, comcon.allowHosts("https://*.example.com"));

var tenant = comcon.include(
    "function(a){ var r = {};"
  + "  function ask(url) { return out.request(url) === undefined ? 'denied' : 'recorded'; }"
  + "  r.inGlob    = ask('https://api.example.com/v1/orders');"
  + "  r.offGlob   = ask('https://evil.net/exfil');"
  + "  r.plainHttp = ask('http://api.example.com/v1');"                       // scheme is exact
  + "  try { out.request('https://api.example.com@evil.net/x'); r.creds = 'accepted'; }"
  + "  catch (e) { r.creds = 'refused'; }"
  + "  r.drain = (out.pending() === undefined) ? 'denied' : 'LEAKED';"        // the host's half
  + "  return r; }",
    { imports: [], grants: { out: narrowed } });

function counts() {
    var d = nginx.tenantDenials().byOp, c = {}, k;
    for (k in d) { if (d[k] > 0) { c[k] = d[k]; } }
    return c;
}

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/outbound"; }).handler = function (req) {
    var o = { tenant: tenant({}) };
    // What the host would now perform (comcon.std.outbound.perform(cap, req)
    // reads the queue, does the I/O, and clears exactly what it performed).
    // The demo does no network: it shows the queue and clears it.
    var q = cap.pending();
    o.hostQueue = { requests: q.requests, dropped: q.dropped };
    cap.clear(q.requests.length);
    o.denials = counts();
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(o));
};

nginx.log(6, "S3: outbound capability narrowed to https://*.example.com");
