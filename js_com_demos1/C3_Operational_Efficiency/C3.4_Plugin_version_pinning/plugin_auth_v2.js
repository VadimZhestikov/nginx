// plugin_auth_v2.js — authentication plugin v2.0.0
// Adds Bearer token support alongside API key validation.
// install(nginx, config) is called by nginx.use().

function install(ng, cfg) {
    var validKeys   = cfg.apiKeys   || ['demo-key-1', 'demo-key-2'];
    var validTokens = cfg.tokens    || ['bearer-token-xyz'];

    var server = ng.http.servers[0];
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

    ng.log(5, 'plugin auth v2.0.0 installed, keys=' + validKeys.length +
        ' tokens=' + validTokens.length);
}
