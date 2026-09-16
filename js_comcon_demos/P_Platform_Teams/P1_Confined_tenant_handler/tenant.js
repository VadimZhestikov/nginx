// THE TENANT — code written by someone you do not trust.
//
// This file is real JavaScript (lint it, test it with qjs), but it never runs
// as host code: host.js reads it as TEXT (String(fn)) and hands it to
// comcon.include(), which compiles it inside a confined compartment.  Nothing
// here can reach `nginx`, `createSocket`, `fetch`, or any host object -- the
// compartment holds only what the contract in host.js granted, and this
// contract grants nothing at all.  (The text is allowed to MENTION `nginx`
// only because host.js lists it in `imports`, so the probe below can run;
// mentioning is not holding.)
//
// Data in: a plain object the host built from the request (a COPY).
// Data out: a plain object; the host turns it into the response.  A returned
// header value carrying CRLF is DROPPED by the host boundary, so a tenant
// cannot smuggle a second header past you.

export default function (req) {
    var count = (req.headers && req.headers["x-count"]) | 0;
    var caged = (typeof nginx === "undefined") ? "yes" : "NO";

    return {
        status: 200,
        headers: {
            "content-type":       "text/plain",
            "X-Tenant":           "acme",
            "X-Tenant-Caged":     caged,
            "X-Tenant-Seen-UA":   (req.headers && req.headers["user-agent"]) || "-",
            "X-Tenant-Try-Inject": "ok\r\nX-Evil: pwned"     // dropped, not smuggled
        },
        body: "tenant handled " + req.method + " " + req.uri
            + " caged=" + caged + " count=" + (count + 1) + "\n"
    };
}
