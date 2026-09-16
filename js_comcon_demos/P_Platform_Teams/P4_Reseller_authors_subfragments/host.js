// THE HOST — grants a reseller the right to admit two sub-fragments.
import reseller from "./reseller.js";

var sock = nginx.createSocket("127.0.0.1:8253");

// `author()` is not a mediation and wraps no host object: it is a descriptor
// that include() grants, and the far side is an object whose only verb is
// `include`.  The reseller's socket wrapper is the ONLY capability a
// sub-fragment can receive, and only narrower.
var acme = comcon.include(String(reseller), {
    imports: [],
    grants: {
        s:      comcon.mediate(sock, comcon.allow(["address", "port"])),
        author: comcon.author({ subFragments: 2 })
    }
});

var locs = nginx.http.servers[0].locations;
locs.find(function (l) { return l.path === "/reseller"; }).handler = function (req) {
    req.respond(200, { "content-type": "application/json" }, JSON.stringify(acme({})));
};

nginx.log(6, "P4: reseller admitted with author({subFragments: 2})");
