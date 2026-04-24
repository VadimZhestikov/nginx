// plugin_auth_v1.js — authentication plugin v1.0.0
// Validates API keys from the X-Api-Key header.
// install(nginx, config) is called by nginx.use().

function install(ng, cfg) {
    var validKeys = cfg.apiKeys || ['demo-key-1', 'demo-key-2'];

    var server = ng.http.servers[0];
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

    ng.log(5, 'plugin auth v1.0.0 installed, keys=' + validKeys.length);
}
