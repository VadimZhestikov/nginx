// C3.1 — Hot-load a new feature plugin without nginx reload
//
// At startup: load plugin_v1/ via nginx.use().
// /admin/load/?version=v2 : hot-swap to plugin_v2/ — no nginx -s reload.
// /api/            : served by whichever plugin is currently active.
// /admin/version/  : returns the currently loaded version.
//
// nginx.use(dirPath, config) evaluates dirPath/index.js; config is
// exposed to the script as nginx.pluginConfig.

var currentVersion = 'v1';

// Load v1 at startup
nginx.use(nginx.cycle.prefix + 'plugin_v1', { env: 'production' });

(function () {
    var server = nginx.http.servers[0];

    // /admin/version/ — report which plugin is loaded
    var versionLoc = server.findLocation('/admin/version/');
    versionLoc.handler = function (r) {
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ version: currentVersion }) + '\n');
    };

    // /admin/load/ — hot-swap the feature plugin
    var loadLoc = server.findLocation('/admin/load/');
    loadLoc.handler = function (r) {
        var version = null;
        r.args.split('&').forEach(function (pair) {
            var kv = pair.split('=');
            if (kv[0] === 'version') version = kv[1];
        });

        if (version !== 'v1' && version !== 'v2') {
            r.respond(400, {}, 'use ?version=v1 or ?version=v2\n');
            return;
        }

        var pluginDir = nginx.cycle.prefix + 'plugin_' + version;
        try {
            nginx.use(pluginDir, { env: 'production', hotloaded: true });
            currentVersion = version;
            r.respond(200, {},
                'loaded plugin ' + version + ' — /api/ now serves ' + version + '\n');
        } catch (e) {
            r.respond(500, {}, 'load failed: ' + e.message + '\n');
        }
    };
}());
