// C1.3 — Pre-computed routing table via SharedWorker
//
// handler.js loads routes.json using std.loadFile, parses it, and sends
// the route map to the SharedWorker on the first request.  The SW stores
// it in a Map for O(1) lookups on all subsequent requests.
//
// All NGINX worker processes share the same SharedWorker thread, so the
// routing table lives in memory exactly once.

import * as std from 'std';

var swPath    = nginx.cycle.prefix + 'sw.js';
var sw        = new SharedWorker(swPath);
var routesObj = null;

// Load routes.json once at startup
(function () {
    var path = nginx.cycle.prefix + 'routes.json';
    try {
        var text = std.loadFile(path);
        if (text) routesObj = JSON.parse(text);
    } catch (e) {
        nginx.log(5, 'C1.3: failed to load routes.json: ' + e.message);
    }
}());

var routesLoaded = false;

function swQuery(msg) {
    return new Promise(function (resolve) {
        var local = new SharedWorker(swPath);
        local.onmessage = function (m) { resolve(m.data); };
        local.postMessage(msg);
    });
}

async function ensureRoutesLoaded() {
    if (routesLoaded) return;
    if (routesObj) {
        await swQuery({ cmd: 'load', routes: routesObj });
        routesLoaded = true;
    }
}

(function () {
    var server = nginx.http.servers[0];

    // /route/ — look up the backend for X-Target-Uri header
    var routeLoc = server.findLocation('/route/');
    routeLoc.handler = async function (r) {
        await ensureRoutesLoaded();
        var targetUri = r.headers['X-Target-Uri'] || r.headers['x-target-uri'] || '/';
        var result    = await swQuery({ cmd: 'route', uri: targetUri });

        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({
                target_uri: targetUri,
                backend:    result.backend || 'default_backend'
            }) + '\n');
    };

    // /admin/routes/ — list all routes in the routing table
    var adminLoc = server.findLocation('/admin/routes/');
    adminLoc.handler = async function (r) {
        await ensureRoutesLoaded();
        var result = await swQuery({ cmd: 'list' });
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify(result.routes || {}, null, 2) + '\n');
    };
}());

// Keep the global SW channel alive (no-op onmessage)
sw.onmessage = function () {};
