// plugin_v2.js — updated feature version with improvements
// Called by nginx.use(path, config) — must define a global install() function.

function install(ng, cfg) {
    var server = ng.http.servers[0];
    var loc = server.findLocation('/api/');
    if (loc) {
        loc.handler = function (r) {
            r.respond(200, {
                'X-Plugin-Version': 'v2',
                'X-Feature-Flags':  'new-algorithm,faster-path'
            }, 'v2 response: hello from plugin v2 (improved!)\n');
        };
    }
    ng.log(5, 'plugin_v2 installed (cfg=' + JSON.stringify(cfg) + ')');
}
