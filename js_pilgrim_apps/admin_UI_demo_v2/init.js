/*
 * admin_UI_demo_v2 — init.js
 */

nginx.use('./js_pilgrim_apps/admin-shell');
nginx.use('js_pilgrim_apps/admin_UI_demo_v2/plugins/metrics');
nginx.use('js_pilgrim_apps/admin_UI_demo_v2/plugins/banner');

(function() {
    var _seeded = false;
    nginx.http.addHook(function() {
        if (_seeded) { return; }
        _seeded = true;
        nginx.shared.set('demo:app',         'admin_UI_demo_v2');
        nginx.shared.set('demo:version',     '2.0.0');
        nginx.shared.set('demo:environment', 'development');
        nginx.shared.set('demo:started_at',  new Date().toISOString());
        nginx.shared.set('demo:workers',     String(nginx.cpu_count));
        nginx.shared.set('config:log_level', 'notice');
        nginx.shared.set('config:max_rps',   '1000');
        nginx.shared.set('feature:beta_ui',  'true');
        nginx.shared.set('feature:tracing',  'false');
    });
}());

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
                            peers.push({ address: p.address, weight: p.weight,
                                         down: p.down, maxFails: p.maxFails });
                        }
                        usInfo.push({ name: us.name, peers: peers });
                    }
                    var body = JSON.stringify({
                        app: nginx.shared.get('demo:app'),
                        version: nginx.shared.get('demo:version'),
                        workerIdx: nginx.workerIdx,
                        workers: nginx.cpu_count,
                        plugins: nginx.plugins.length,
                        upstreams: usInfo
                    }, null, 2);
                    req.respond(200, { 'Content-Type': 'application/json' },
                                body + '\n');
                };
            }

            if (loc.path === '/health') {
                loc.handler = function(req) {
                    req.respond(200, { 'Content-Type': 'application/json' },
                                '{"status":"ok","worker":' +
                                nginx.workerIdx + '}\n');
                };
            }
        }
    }
}());

nginx.log(6, 'admin_UI_demo_v2: ready');
