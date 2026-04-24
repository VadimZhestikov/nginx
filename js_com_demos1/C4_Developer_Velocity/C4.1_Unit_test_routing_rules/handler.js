// handler.js — uses the same routing logic as route.js in nginx context
//
// Because route.js is a pure ES module with no platform dependencies,
// the same function runs in:
//   - qjs route-test.js  (unit tests, no nginx)
//   - nginx handler      (production routing)
//   - browser            (client-side route preview)
//
// In nginx, we inline the matchRoute logic (nginx doesn't support ES
// module imports in js_source scripts yet); in production use js_preprocess
// or bundle the logic into a single file.

function matchRoute(uri, headers) {
    var accept = (headers && headers['accept']) || '';

    if (uri.startsWith('/api/')) {
        if (accept && accept.indexOf('json') !== -1)
            return 'json_backend';
        return 'api_backend';
    }
    if (uri.startsWith('/static/'))
        return 'cdn_backend';
    if (uri === '/health' || uri === '/health/')
        return 'health_backend';
    if (uri.startsWith('/admin/')) {
        if (headers && headers['x-internal'] === 'true')
            return 'admin_backend';
        return 'forbidden';
    }
    return 'default_backend';
}

(function () {
    var server = nginx.http.servers[0];
    var routeLoc = server.findLocation('/route/');

    routeLoc.handler = function (r) {
        // Extract the URI to route from ?uri= query param or X-Target-Uri header
        var targetUri = r.headers['X-Target-Uri'] || r.headers['x-target-uri'] || '/';
        var paramMatch = r.args.match(/(?:^|&)uri=([^&]*)/);
        if (paramMatch) targetUri = decodeURIComponent(paramMatch[1]);

        var backend = matchRoute(targetUri, r.headers);

        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ uri: targetUri, backend: backend }) + '\n');
    };
}());
