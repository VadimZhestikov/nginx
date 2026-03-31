/*
 * admin_UI_demo — init.js
 *
 * Loads the admin-shell package and two sample plugins, then seeds
 * nginx.shared with demo data and wires up the /info and /health
 * location handlers.
 */

/* ------------------------------------------------------------------ */
/* 1. Load plugins                                                      */
/* ------------------------------------------------------------------ */

/* Admin shell: installs the WebSocket handler on /admin/ws */
nginx.use('./js_pilgrim_apps/admin-shell');

/* Metrics: counts requests per location, stored in nginx.shared */
nginx.use('js_pilgrim_apps/admin_UI_demo/plugins/metrics');

/* Banner: adds X-Powered-By response header */
nginx.use('js_pilgrim_apps/admin_UI_demo/plugins/banner');

/* ------------------------------------------------------------------ */
/* 2. Seed shared state with demo data (deferred to first request)     */
/* ------------------------------------------------------------------ */

/* nginx.shared is only accessible in worker request handlers, not at  */
/* config-parse time.  Use a one-shot addHook to seed the values on    */
/* the first request that reaches any worker.                           */
(function() {
    var _seeded = false;
    nginx.http.addHook(function() {
        if (_seeded) { return; }
        _seeded = true;
        nginx.shared.set('demo:app',         'admin_UI_demo');
        nginx.shared.set('demo:version',     '1.0.0');
        nginx.shared.set('demo:environment', 'development');
        nginx.shared.set('demo:started_at',  new Date().toISOString());
        nginx.shared.set('demo:workers',     String(nginx.cpu_count));
        nginx.shared.set('config:log_level', 'notice');
        nginx.shared.set('config:max_rps',   '1000');
        nginx.shared.set('feature:beta_ui',  'false');
        nginx.shared.set('feature:tracing',  'false');
    });
}());

/* ------------------------------------------------------------------ */
/* 3. Wire up /info and /health handlers                                */
/* ------------------------------------------------------------------ */

(function() {
    var servers = nginx.http.servers;
    for (var si = 0; si < servers.length; si++) {
        var locs = servers[si].locations;
        for (var li = 0; li < locs.length; li++) {
            var loc = locs[li];

            if (loc.path === '/info') {
                loc.handler = function(req) {
                    var upstreams = nginx.http.upstreams;
                    var usInfo = [];
                    for (var i = 0; i < upstreams.length; i++) {
                        var us = upstreams[i];
                        var peers = [];
                        for (var j = 0; j < us.peers.length; j++) {
                            var p = us.peers[j];
                            peers.push({
                                address:  p.address,
                                weight:   p.weight,
                                down:     p.down,
                                maxFails: p.maxFails
                            });
                        }
                        usInfo.push({ name: us.name, peers: peers });
                    }

                    var body = JSON.stringify({
                        app:       nginx.shared.get('demo:app'),
                        version:   nginx.shared.get('demo:version'),
                        env:       nginx.shared.get('demo:environment'),
                        started:   nginx.shared.get('demo:started_at'),
                        workers:   nginx.cpu_count,
                        plugins:   nginx.plugins.length,
                        upstreams: usInfo
                    }, null, 2);

                    req.respond(200, { 'Content-Type': 'application/json' },
                                body + '\n');
                };
            }

            if (loc.path === '/health') {
                loc.handler = function(req) {
                    req.respond(200, { 'Content-Type': 'application/json' },
                                '{"status":"ok"}\n');
                };
            }
        }
    }
}());

nginx.log(6, 'admin_UI_demo: ready — open http://localhost:8080/admin/');
