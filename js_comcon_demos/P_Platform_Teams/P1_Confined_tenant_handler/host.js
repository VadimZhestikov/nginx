// THE HOST — your code, holding all the authority, giving the tenant none.
//
// One pipeline, no new nginx directive:  comcon.include(source, contract)
// compiles the tenant at CONFIG TIME (stage-0, before the workers fork) in a
// confined compartment; the returned callable is bound to a location through
// the ordinary js_com `location.handler`.  Per request the host marshals a
// data-only view of the request in, and a data-only answer out.
import tenant from "./tenant.js";

// `imports` is the admission contract: the ONLY free names the fragment may
// mention.  A name that is not listed is refused at config load
// (E_ADMIT_FREENAME) -- before any request ever reaches the fragment.  `nginx`
// is listed here so the tenant may PROBE it; listing a name only lets the text
// mention it.  What the name IS comes from `grants`, and nothing grants nginx,
// so inside the compartment it reads as undefined.  Two walls: admission
// decides what may be named, the environment decides what a name holds.
var handle = comcon.include(String(tenant), { imports: ["nginx"] });

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/t"; }).handler = function (req) {
    var out = handle({ method: req.method, uri: req.uri, headers: req.headers });
    req.respond(out.status, out.headers, out.body);
};

// The host's own view of what the tenant tried: {mode, total, byOp}.
locs.find(function (l) { return l.path === "/denials"; }).handler = function (req) {
    req.respond(200, { "content-type": "application/json" },
                JSON.stringify(nginx.tenantDenials()));
};

nginx.log(6, "P1: tenant admitted at config load; /t is confined, /denials is the host's");
