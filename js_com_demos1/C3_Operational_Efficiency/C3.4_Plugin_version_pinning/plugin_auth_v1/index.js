// plugin_auth_v1/index.js — authentication plugin v1.0.0
// Validates API keys from the X-Api-Key header.
// nginx.pluginConfig holds the config passed from nginx.use().

(function () {
    var cfg       = nginx.pluginConfig || {};
    var validKeys = cfg.apiKeys || ['demo-key-1', 'demo-key-2'];

    var server = nginx.http.servers[0];
    var loc = server.findLocation('/api/');
    if (!loc) return;

    loc.handler = function (r) {
        var key = r.headers['X-Api-Key'] || r.headers['x-api-key'] || '';
        if (validKeys.indexOf(key) === -1) {
            r.respond(401, { 'X-Plugin': 'auth-v1' },
                JSON.stringify({ error: 'invalid api key' }) + '\n');
            return;
        }
        r.respond(200, {
            'X-Plugin':         'auth-v1',
            'X-Plugin-Version': '1.0.0'
        }, JSON.stringify({ ok: true, plugin: 'auth', version: '1.0.0', key: key }) + '\n');
    };

    nginx.log(5, 'plugin auth v1.0.0 installed, keys=' + validKeys.length);
}());
