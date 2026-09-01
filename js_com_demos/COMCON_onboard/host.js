// COMCON onboarding demo — the HOST program.
//
// It runs the tenant in learn mode, then serves the GENERATED CONTRACT STUB on
// /contract by feeding nginx.tenantLearning() through the reusable generator.
// The generator is a plain library module (onboard.js) — no privileged tooling.

import { generateContract } from "./onboard.js";

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/contract") {
        locs[i].handler = function (req) {
            var stub = generateContract(nginx.tenantLearning());
            req.respond(200, { "content-type": "text/plain" }, stub);
        };
    }
}

nginx.log(6, "COMCON onboard: host ready (/contract generates the grant stub)");
