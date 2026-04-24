// plugin_auth_v2/index.js — authentication plugin v2.0.0
// Adds Bearer token support alongside API key validation.
// nginx.pluginConfig holds the config passed from nginx.use().

(function () {
    var cfg         = nginx.pluginConfig || {};
    var validKeys   = cfg.apiKeys   || ['demo-key-1', 'demo-key-2'];
    var validTokens = cfg.tokens    || ['bearer-token-xyz'];

    var server = nginx.http.servers[0];
    var loc = server.findLocation('/api/');
    if (!loc) return;

    loc.handler = function (r) {
        var key   = r.headers['X-Api-Key']    || r.headers['x-api-key']    || '';
        var auth  = r.headers['Authorization'] || r.headers['authorization'] || '';
        var token = auth.replace(/^Bearer\s+/, '');

        var keyOk   = validKeys.indexOf(key) !== -1;
        var tokenOk = token && validTokens.indexOf(token) !== -1;

        if (!keyOk && !tokenOk) {
            r.respond(401, { 'X-Plugin': 'auth-v2' },
                JSON.stringify({ error: 'invalid credentials' }) + '\n');
            return;
        }

        var method = keyOk ? 'api-key' : 'bearer';
        r.respond(200, {
            'X-Plugin':         'auth-v2',
            'X-Plugin-Version': '2.0.0',
            'X-Auth-Method':    method
        }, JSON.stringify({
            ok:      true,
            plugin:  'auth',
            version: '2.0.0',
            method:  method
        }) + '\n');
    };

    nginx.log(5, 'plugin auth v2.0.0 installed, keys=' + validKeys.length +
        ' tokens=' + validTokens.length);
}());
