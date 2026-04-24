// plugin_v1.js — initial feature version
// Called by nginx.use(path, config) — must define a global install() function.

function install(ng, cfg) {
    var server = ng.http.servers[0];
    var loc = server.findLocation('/api/');
    if (loc) {
        loc.handler = function (r) {
            r.respond(200, { 'X-Plugin-Version': 'v1' },
                'v1 response: hello from plugin v1\n');
        };
    }
    ng.log(5, 'plugin_v1 installed (cfg=' + JSON.stringify(cfg) + ')');
}
