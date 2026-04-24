// C1.2 — Fan-out / fan-in with sequential subrequests
//
// /merged/ fans out to three internal micro-services (alpha, beta, gamma),
// collects their JSON responses, and returns a combined object.
//
// Note: Promise.all() over subrequests is not yet supported; we use
// sequential awaits.  The pattern still demonstrates how JS lets you
// compose multiple internal data sources into a single response without
// any Lua/Perl middleware.

(function () {
    var server = nginx.http.servers[0];

    // ── Internal "micro-services" ────────────────────────────────────────

    var alphaLoc = server.findLocation('/internal/alpha/');
    alphaLoc.handler = function (r) {
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ service: 'alpha', value: 42, unit: 'requests/s' }) + '\n');
    };

    var betaLoc = server.findLocation('/internal/beta/');
    betaLoc.handler = function (r) {
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ service: 'beta', value: 100, unit: 'ms latency' }) + '\n');
    };

    var gammaLoc = server.findLocation('/internal/gamma/');
    gammaLoc.handler = function (r) {
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ service: 'gamma', value: 7, unit: 'active workers' }) + '\n');
    };

    // ── Fan-out aggregator ───────────────────────────────────────────────

    var mergedLoc = server.findLocation('/merged/');
    mergedLoc.handler = async function (r) {
        // Sequential awaits (Promise.all over subrequests not yet supported)
        var a = await r.subrequest('/internal/alpha/');
        var b = await r.subrequest('/internal/beta/');
        var g = await r.subrequest('/internal/gamma/');

        var result = {
            merged: true,
            sources: [
                JSON.parse(a.body),
                JSON.parse(b.body),
                JSON.parse(g.body)
            ],
            total_value: JSON.parse(a.body).value +
                         JSON.parse(b.body).value +
                         JSON.parse(g.body).value
        };

        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify(result, null, 2) + '\n');
    };
}());
