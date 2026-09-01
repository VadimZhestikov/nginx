// COMCON dogfood — the CONFINED TENANT (compartment 1, deny-by-default).
//
// This is a mirror-style policy: count requests, echo a request header, and
// tag the response. It is exactly the M1 "count + tag" shape — but written by
// an untrusted party and caged by the environment it was handed, not by trust.
//
// Its whole world is the names granted to it: report, onRequest, and the
// host's `granted` socket. There is no `nginx`, no createSocket, no eval-out.
// Its only authority over the response is the value it returns.

var count = 0;   // per-worker (cross-worker shared state = a later capability
                 // grant; the zero-capability request path counts locally).

onRequest(function (req) {
    count++;

    // Prove the cage from *inside a live request*:
    //  - deny-by-default: the host surface is unnameable,
    //  - the A1 reach gate: we HOLD `granted` but cannot walk it to config.
    var caged = (typeof nginx === "undefined")
                && (typeof granted === "object")
                && (granted.listener === null);

    var ua = (req.headers && req.headers["user-agent"]) || "-";

    return {
        status: 200,
        headers: {
            "X-Mirror-Count": String(count),
            "X-Mirror-Seen-UA": ua,
            "X-Mirror-Caged": caged ? "yes" : "LEAK",
            "content-type": "text/plain",
            // A tenant cannot smuggle a second header via CRLF — dropped:
            "X-Mirror-Try-Inject": "a\r\nX-Evil: 1"
        },
        body: "mirror " + req.method + " " + req.uri +
              " count=" + count + " caged=" + (caged ? "yes" : "LEAK") + "\n"
    };
});

report("COMCON dogfood: tenant handler registered (caged mirror)");
