// THE HOST — the tenant's reference manual is a query over what it holds.
var sock = nginx.createSocket("127.0.0.1:8268");
var srv  = nginx.http.servers[0];

var acme = {
    imports: ["JSON", "s", "http", "out", "author"],
    grants: {
        s:      comcon.mediate(comcon.mediate(sock, comcon.redact(["fd", "listener"])), comcon.uses("acme:s", 100, 60)),
        http:   comcon.mediate(srv, comcon.routes("/acme/*")),
        out:    comcon.mediate(comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.example.com")),
                    comcon.window({ days: "Mon-Fri", from: "09:00", to: "17:00" })),
        author: comcon.author({ subFragments: 4 })
    },
    meter: comcon.meter({ timeoutMs: 250, retainedBytes: 1048576 }),
    checkRequest: true, tests: "function(f){}",
    deps: []
};
var frag = comcon.include("function(req){ return { ok: true }; }", acme);

// the binding store, so ops.docs can render by name with the live epoch
var target = nginx.http.servers[0].locations.find(function (l) { return l.path === "/acme"; });
var h = comcon.bindAt(function (callable, epoch) {
    target.handler = function (req) { req.respond(200, { "x-epoch": String(epoch), "content-type": "application/json" }, JSON.stringify(callable({}))); };
}, comcon.quote("function(){ return { ok: true }; }"), acme);
var ops = comcon.std.ops({ bindings: true });
ops.register("acme", h, comcon.quote("function(){ return { ok: true }; }"));

var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }
at("/docs").handler   = function (req) { req.respond(200, { "content-type": "text/markdown" }, ops.docs("acme")); };
at("/model").handler  = function (req) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(comcon.std.docs.model("acme", frag), null, 1)); };
at("/narrow").handler = function (req) {
    // the policy changes: the same manual, re-queried, says something else
    h.replace(comcon.quote("function(){ return { ok: true, v: 2 }; }"));
    req.respond(200, { "content-type": "text/markdown" }, ops.docs("acme"));
};
nginx.log(6, "A3: /docs, /model, /narrow, /acme");
