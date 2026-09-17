// THE HOST — the grant is a switch we hold at run time.
//
// A tenant ("acme") serves /api with a fragment that holds three grants: an
// outbound capability narrowed to the vendor's hosts (`out`), a socket (`s`)
// and an author capability.  The fragment delegates a COPY of `out` to a
// vendor library it authors as a sub-fragment and keeps across calls -- the
// shape of "a library runs with what we granted, not with what it wants".
//
// CVE day: the operator withdraws `out`.  No reload, no redeploy.  The
// tenant's own calls AND the library's calls on the copy answer cap.revoked
// from the next request, in every posture; a new version the tenant pushes
// (replace) gets the same dead grant; nothing but a new admission brings it
// back.  Offboarding is the same verb with no grant name.

var sock = nginx.createSocket("127.0.0.1:8269");
var vendor = comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.vendor.example"));

// The binding's environment: what realize() may hand the fragment, by name.
var renv = comcon.env();
renv = comcon.grant(renv, "out", vendor);
renv = comcon.grant(renv, "s", sock);
renv = comcon.grant(renv, "author", comcon.author({ subFragments: 1 }));

// The tenant's text.  The library is authored once and kept: its `out` is a
// copy of the tenant's, narrowed by nothing, held by a different fragment.
var LIB = "function(b){ return out.request('https://cdn.vendor.example/asset') === undefined ? 'denied' : 'fetched'; }";
var V1 =
    "(function(){ var lib = null; return function(a){"
  + "  if (lib === null) { lib = author.include(" + JSON.stringify(LIB) + ", { imports: [], grants: { out: out } }); }"
  + "  return { direct: out.request('https://api.vendor.example/v1') === undefined ? 'denied' : 'fetched',"
  + "           viaLibrary: lib({}), port: typeof s.port }; }; })()";
var V2 = V1.replace("'https://api.vendor.example/v1'", "'https://api.vendor.example/v2'");

var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }
function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }

// The site: how an epoch is installed at /api.
var api = at("/api");
function site(callable, epoch) {
    api.handler = callable === null
        ? function (req) { req.respond(410, { "content-type": "text/plain" }, "gone"); }
        : function (req) {
              var r = callable({});
              r.epoch = epoch;
              r.denials = nginx.tenantDenials().byOp["cap.revoked"] || 0;
              respond(req, r);
          };
}

var q1 = comcon.quote(V1);
var h = comcon.bindAt(site, q1, { imports: ["out", "s", "author"], env: renv });

// The operator session holds the binding by name; withdraw is class X, so
// the confirmation must NAME the binding -- a pasted {confirm: true} from
// another call cannot withdraw the wrong tenant's grant.
var ops = comcon.std.ops({ log: nginx.tenantDenials, mode: comcon.mode, bindings: true });
ops.register("acme", h, q1);

at("/ops").handler = function (req) {
    var args = String(req.args || ""), m = {}, r = {};
    args.split("&").forEach(function (kv) { var p = kv.split("="); if (p[0]) { m[p[0]] = p[1] || ""; } });
    try {
        if (m.op === "withdraw")      r = ops.withdraw("acme", m.grant, { confirm: "acme" });
        else if (m.op === "noconfirm") { ops.withdraw("acme", m.grant); r.accepted = true; }
        else if (m.op === "status")   r = { withdrawn: ops.withdrawn("acme"), epoch: h.epoch(),
                                            mode: nginx.tenantDenials().mode, denials: nginx.tenantDenials().byOp["cap.revoked"] || 0 };
        else if (m.op === "replace")  r = { epoch: h.replace(comcon.quote(V2)) };
        else if (m.op === "rollback") r = { epoch: h.rollback() };
        else if (m.op === "audit")    r = { mode: comcon.mode("audit") };
        else if (m.op === "enforce")  r = { mode: comcon.mode("enforce") };
        else if (m.op === "docs")     r = { docs: ops.docs("acme") };
        else if (m.op === "offboard") r = ops.withdraw("acme", { confirm: "acme" });
        else r = { error: "unknown op" };
    } catch (e) { r = { error: e.name + ": " + e.message }; }
    respond(req, r);
};
