// plugin_v2/index.js — updated feature version with improvements
// Loaded by nginx.use('./plugin_v2', config).
// nginx.pluginConfig holds the config passed to nginx.use().

(function () {
    var cfg = nginx.pluginConfig || {};
    var server = nginx.http.servers[0];
    var loc = server.findLocation('/api/');
    if (loc) {
        loc.handler = function (r) {
            r.respond(200, {
                'X-Plugin-Version': 'v2',
                'X-Feature-Flags':  'new-algorithm,faster-path'
            }, 'v2 response: hello from plugin v2 (improved!)\n');
        };
    }
    nginx.log(5, 'plugin_v2 installed (env=' + (cfg.env || 'unknown') + ')');
}());
