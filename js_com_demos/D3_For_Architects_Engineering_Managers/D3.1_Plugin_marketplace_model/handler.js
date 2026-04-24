// D3.1 — Plugin Marketplace Model
//
// Demonstrates nginx.install() — the inline plugin system that lets teams
// compose nginx behaviour from independent, testable modules.
//
// Plugins installed here:
//   ratelimit@1.0 — token-bucket rate limiter via SAB (10 req total)
//   cors@2.1      — adds Access-Control-Allow-Origin to every /api/ response
//   telemetry@1.0 — counts requests and exposes /metrics/
//
// Each plugin is a plain JS object with an .install(config) method.
// nginx.install(plugin, config) calls that method synchronously.
//
// A plugin registry (plain JS object) tracks installed plugins so that
// GET /plugins/ can enumerate them.  nginx.plugins is populated only by
// nginx.use() (filesystem plugins); inline nginx.install() plugins are
// tracked here in a local registry array.

(function () {
    var server  = nginx.http.servers[0];
    var apiLoc  = server.findLocation('/api/');
    var pluginsLoc  = server.findLocation('/plugins/');
    var metricsLoc  = server.findLocation('/metrics/');

    // Local registry — nginx.install() does not add to nginx.plugins
    var registry = [];

    function registerPlugin(name, version, meta) {
        registry.push({ name: name, version: version, meta: meta || {} });
    }

    // ================================================================
    // Plugin: ratelimit@1.0
    // ================================================================
    var ratelimitPlugin = {
        name: 'ratelimit',
        version: '1.0',

        install: function (cfg) {
            var max    = cfg.max    || 10;
            var sab    = new SharedArrayBuffer(4);
            var arr    = new Int32Array(sab);
            Atomics.store(arr, 0, max);

            // Hook: consume a token or reject with 429
            apiLoc.addHook(function (r) {
                var old = Atomics.add(arr, 0, -1);
                if (old <= 0) {
                    Atomics.add(arr, 0, 1);  // restore
                    r.respond(429, {
                        'X-RateLimit-Remaining': '0',
                        'Retry-After': '1'
                    }, 'Too Many Requests\n');
                } else {
                    r.setHeader('X-RateLimit-Remaining', String(old - 1));
                }
            });

            // Expose the SAB counter for telemetry
            ratelimitPlugin._arr = arr;
            ratelimitPlugin._max = max;
        },

        _arr: null,
        _max: 10
    };

    nginx.install(ratelimitPlugin, { max: 10 });
    registerPlugin('ratelimit', '1.0', { description: 'Token-bucket rate limiter' });

    // ================================================================
    // Plugin: cors@2.1
    // ================================================================
    var corsPlugin = {
        name: 'cors',
        version: '2.1',

        install: function (cfg) {
            var origin  = cfg.origin  || '*';
            var methods = cfg.methods || 'GET, POST, OPTIONS';
            var headers = cfg.headers || 'Content-Type, Authorization';

            // Hook: inject CORS headers before the content handler runs
            apiLoc.addHook(function (r) {
                r.setHeader('Access-Control-Allow-Origin',  origin);
                r.setHeader('Access-Control-Allow-Methods', methods);
                r.setHeader('Access-Control-Allow-Headers', headers);

                // Short-circuit OPTIONS preflight
                if (r.method === 'OPTIONS') {
                    r.respond(204, {}, '');
                }
            });
        }
    };

    nginx.install(corsPlugin, {
        origin:  '*',
        methods: 'GET, POST, OPTIONS',
        headers: 'Content-Type, Authorization'
    });
    registerPlugin('cors', '2.1', { description: 'CORS header injection' });

    // ================================================================
    // Plugin: telemetry@1.0
    // ================================================================
    var telemetryPlugin = {
        install: function (cfg) {
            var sab = new SharedArrayBuffer(8);
            var arr = new Int32Array(sab);  // [0]=total, [1]=errors

            apiLoc.addHook(function (r) {
                Atomics.add(arr, 0, 1);
            });

            // Expose /metrics/ via the telemetry plugin
            metricsLoc.handler = function (r) {
                var total  = Atomics.load(arr, 0);
                var remain = ratelimitPlugin._arr
                    ? Atomics.load(ratelimitPlugin._arr, 0)
                    : -1;
                r.respond(200, {
                    'Content-Type': 'application/json'
                }, JSON.stringify({
                    api_requests_total: total,
                    ratelimit_tokens_remaining: remain < 0 ? null : remain,
                    ratelimit_max: ratelimitPlugin._max
                }, null, 2) + '\n');
            };
        }
    };

    nginx.install(telemetryPlugin, {});
    registerPlugin('telemetry', '1.0', { description: 'Request counter and metrics' });

    // ================================================================
    // /api/ — content handler (runs after all hooks pass)
    // ================================================================
    apiLoc.handler = function (r) {
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            message: 'API response',
            note: 'CORS headers added by cors@2.1 plugin'
        }) + '\n');
    };

    // ================================================================
    // /plugins/ — list installed plugins
    // ================================================================
    pluginsLoc.handler = function (r) {
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            installed: registry,
            // nginx.plugins tracks filesystem plugins loaded via nginx.use()
            filesystem_plugins: nginx.plugins
        }, null, 2) + '\n');
    };

})();
