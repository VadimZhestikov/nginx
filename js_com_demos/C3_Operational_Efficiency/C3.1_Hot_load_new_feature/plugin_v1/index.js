// plugin_v1/index.js — initial feature version
// Loaded by nginx.use('./plugin_v1', config).
// nginx.pluginConfig holds the config passed to nginx.use().

(function () {
    var cfg = nginx.pluginConfig || {};
    var server = nginx.http.servers[0];
    var loc = server.findLocation('/api/');
    if (loc) {
        loc.handler = function (r) {
            r.respond(200, { 'X-Plugin-Version': 'v1' },
                'v1 response: hello from plugin v1\n');
        };
    }
    nginx.log(5, 'plugin_v1 installed (env=' + (cfg.env || 'unknown') + ')');
}());
