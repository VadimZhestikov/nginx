// THE HOST — the policy targets by name, but PINS by content hash.
//
// Two pins, two levels.  A dependency is `deps: [{name, path, sha256}]`: read,
// verified against its SHA-256, evaluated as a bare pure script, bound as a
// per-fragment closure parameter (never onto a shared global).  A fragment is
// `identity: <pin>` where pin = sha256_hex(sha256(source) ‖ "c2-tenant-env-1").
// Either mismatch refuses the include -- before the code exists as a callable.
//
// The demo takes the hashes from the query string so test.sh can compute them
// from the same bytes with sha256sum / perl: what is asserted is that the tree
// and an outside tool agree on what was reviewed.

var CHECKOUT = "function(req){ return { ok: true, total: (req.items | 0) * 250, note: 'reviewed' }; }";

function respond(req, o) { req.respond(200, { "content-type": "application/json" }, JSON.stringify(o)); }
function attempt(fn) {
    try { var f = fn(); return { admitted: true, result: f({ items: 3, method: "GET" }) }; }
    catch (e) { return { admitted: false, code: e.code || null,
                         why: String(e.message || e).replace(/^comcon\.include: /, "") }; }
}
function param(req, k) {
    var m = String(req.args || "").match(new RegExp("(?:^|&)" + k + "=([^&]*)"));
    return m ? decodeURIComponent(m[1]) : "";
}
var locs = nginx.http.servers[0].locations;
function at(p) { return locs.find(function (l) { return l.path === p; }); }

// The exact text the host will include: what a reviewer signs off on.
at("/source").handler = function (req) { req.respond(200, { "content-type": "text/plain" }, CHECKOUT); };

// ?identity=<pin>&drift=0|1  -- drift appends one space: Saturday's "patch"
at("/pin").handler = function (req) {
    var pin = param(req, "identity"), drift = param(req, "drift") === "1";
    var src = drift ? CHECKOUT + " " : CHECKOUT;
    var r = attempt(function () { return comcon.include(src, { imports: [], identity: pin }); });
    r.drift = drift; respond(req, r);
};

// ?sha256=<hex>&path=<abs path>  -- the library, pinned
at("/dep").handler = function (req) {
    var sha = param(req, "sha256"), path = param(req, "path");
    var r = attempt(function () {
        return comcon.include(
            "function(req){ return lib.version + ' ' + lib.slug('Hello, World!') + ' ' + lib.clamp(req.items, 1, 2); }",
            { imports: ["lib"], deps: [{ name: "lib", path: path, sha256: sha }] });
    });
    r.path = path.replace(/^.*\//, ""); respond(req, r);
};

nginx.log(6, "L3: /source, /pin?identity=&drift=, /dep?sha256=&path=");
