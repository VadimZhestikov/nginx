// A0.1 — Custom nginx property via nginx.use()
//
// Every other demo in this collection builds on the same two-step pattern:
//
//   Step 1 — extend the nginx COM object:
//     nginx.use('./plugin', config)
//     After this call, nginx.featureFlags (or nginx.admin, nginx.rateLimit, …)
//     is a first-class property on the nginx object, indistinguishable from
//     the built-in nginx.shared or nginx.http.
//
//   Step 2 — install handlers that USE the extension:
//     nginx.broadcast(fn)
//     fn runs in every worker during init_process; it can call nginx.featureFlags
//     just like any built-in property.
//
// The three layers:
//
//   nginx C core
//     └── JS COM bindings  (nginx.http, nginx.shared, nginx.broadcast, nginx.use)
//             └── user-defined JS API  (nginx.featureFlags — defined by this demo)
//                     └── application logic  (handlers below)

// ── Step 1: extend the nginx COM object ──────────────────────────────────────
nginx.use('./feature-flags', {
    flags: ['dark-mode', 'new-checkout']
});

// ── Step 2: install request handlers ─────────────────────────────────────────
//
// nginx.featureFlags is now available here and in every worker exactly like
// nginx.shared or nginx.http — the COM extension is transparent to callers.
nginx.broadcast(function () {
    var locs = nginx.http.servers[0].locations;

    function loc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    /* ------------------------------------------------------------------ *
     * /flags/          GET  — list all flags
     * /flags/enable/   POST — body: {"flag":"name"} — enable a flag
     * /flags/disable/  POST — body: {"flag":"name"} — disable a flag
     *
     * All three paths are served by the /flags/ prefix location.
     * ------------------------------------------------------------------ */
    var flagsLoc = loc('/flags/');
    if (flagsLoc) {
        flagsLoc.handler = async function (r) {
            var uri = r.uri;
            var m   = r.method;

            if (m === 'GET' && uri === '/flags/') {
                r.respond(200, {'Content-Type': 'application/json'},
                    JSON.stringify(nginx.featureFlags.list()) + '\n');
                return;
            }

            if (m === 'POST' &&
                (uri === '/flags/enable/' || uri === '/flags/disable/')) {
                var raw = await r.readBody();
                var body;
                try { body = JSON.parse(raw); } catch (e) {
                    r.respond(400, {}, 'invalid JSON\n'); return;
                }
                if (!body || !body.flag) {
                    r.respond(400, {}, 'flag required\n'); return;
                }
                nginx.featureFlags.set(body.flag, uri === '/flags/enable/');
                r.respond(200, {'Content-Type': 'application/json'},
                    JSON.stringify(nginx.featureFlags.list()) + '\n');
                return;
            }

            r.respond(404, {}, 'not found\n');
        };
    }

    /* ------------------------------------------------------------------ *
     * GET /api/ — gated by the new-checkout flag.
     * Reads nginx.featureFlags.get() on every request — no restart needed
     * when the flag changes.
     * ------------------------------------------------------------------ */
    var apiLoc = loc('/api/');
    if (apiLoc) {
        apiLoc.handler = function (r) {
            if (!nginx.featureFlags.get('new-checkout')) {
                r.respond(404, {}, 'not found\n');
                return;
            }
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify({
                    ok:      true,
                    version: 'new-checkout',
                    worker:  r.variable('pid')
                }) + '\n');
        };
    }
});
