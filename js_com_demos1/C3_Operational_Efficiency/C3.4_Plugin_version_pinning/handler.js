// C3.4 — Plugin version pinning
//
// Demonstrates loading versioned plugins with configuration via nginx.use().
// The plugin registry tracks name, version, and load time of each plugin.
// /plugins/ returns the registry as JSON.
// /admin/load-plugin/ hot-loads a named plugin version at runtime.
//
// nginx.use(dirPath, config) evaluates dirPath/index.js; config is exposed
// as nginx.pluginConfig inside the plugin script.
//
// The registry tracks which plugin versions are loaded and when, enabling
// auditable, reproducible deployments.

var pluginRegistry = [];

function loadPlugin(name, version, dirPath, config) {
    config = config || {};
    nginx.use(nginx.cycle.prefix + dirPath, config);
    // Record in registry (overwrite if same name)
    var idx = -1;
    for (var i = 0; i < pluginRegistry.length; i++) {
        if (pluginRegistry[i].name === name) { idx = i; break; }
    }
    var entry = {
        name:     name,
        version:  version,
        dirPath:  dirPath,
        loadedAt: new Date().toISOString(),
        config:   config
    };
    if (idx >= 0) pluginRegistry[idx] = entry;
    else pluginRegistry.push(entry);
    nginx.log(5, 'plugin registry: loaded ' + name + '@' + version);
}

// Load auth plugin v1 at startup with pinned configuration
loadPlugin('acmecorp/auth', '1.0.0', 'plugin_auth_v1', {
    apiKeys: ['demo-key-1', 'demo-key-2', 'prod-key-abc']
});

(function () {
    var server = nginx.http.servers[0];

    // /plugins/ — return the plugin registry
    var pluginsLoc = server.findLocation('/plugins/');
    pluginsLoc.handler = function (r) {
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ plugins: pluginRegistry }, null, 2) + '\n');
    };

    // /admin/load-plugin/ — hot-load a plugin by name + version
    var loadLoc = server.findLocation('/admin/load-plugin/');
    loadLoc.handler = function (r) {
        var name    = null;
        var version = null;
        r.args.split('&').forEach(function (pair) {
            var kv = pair.split('=');
            if (kv[0] === 'name')    name    = decodeURIComponent(kv[1] || '');
            if (kv[0] === 'version') version = decodeURIComponent(kv[1] || '');
        });

        if (!name || !version) {
            r.respond(400, {}, 'use ?name=acmecorp/auth&version=2.0.0\n');
            return;
        }

        // Map name+version to a plugin directory
        var dirMap = {
            'acmecorp/auth@1.0.0': 'plugin_auth_v1',
            'acmecorp/auth@2.0.0': 'plugin_auth_v2'
        };
        var key = name + '@' + version;
        var dir = dirMap[key];
        if (!dir) {
            r.respond(404, {}, 'unknown plugin: ' + key + '\n');
            return;
        }

        try {
            loadPlugin(name, version, dir, {
                apiKeys: ['demo-key-1', 'demo-key-2', 'prod-key-abc'],
                tokens:  ['bearer-token-xyz']
            });
            r.respond(200, { 'Content-Type': 'application/json' },
                JSON.stringify({ ok: true, loaded: key }) + '\n');
        } catch (e) {
            r.respond(500, {}, 'load failed: ' + e.message + '\n');
        }
    };
}());
