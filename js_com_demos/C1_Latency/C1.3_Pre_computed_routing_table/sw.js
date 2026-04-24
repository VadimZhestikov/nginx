// C1.3 SharedWorker — pre-computed routing table
//
// Receives a pre-parsed route map from the handler (as a plain object).
// Stores it in a Map and answers O(1) lookup queries.
// Uses onconnect + port.onmessage protocol.
//
// Protocol:
//   {cmd:'load',   routes:{...}}         → {ok:true, count:N}
//   {cmd:'route',  uri:'...'}            → {backend:'...'}
//   {cmd:'list'}                         → {routes:{...}}

var routeMap = null;

function lookup(uri) {
    if (!routeMap || routeMap.size === 0) return null;
    var parts = uri.split('/').filter(function (p) { return p !== ''; });
    for (var i = parts.length; i >= 0; i--) {
        var candidate = '/' + parts.slice(0, i).join('/') + (i > 0 ? '/' : '');
        if (routeMap.has(candidate)) return routeMap.get(candidate);
    }
    return null;
}

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (msg) {
        var data = msg.data;

        if (data.cmd === 'load') {
            routeMap = new Map();
            var obj = data.routes || {};
            Object.keys(obj).forEach(function (k) { routeMap.set(k, obj[k]); });
            port.postMessage({ ok: true, count: routeMap.size });

        } else if (data.cmd === 'route') {
            var backend = lookup(data.uri || '/');
            port.postMessage({ backend: backend || 'default_backend' });

        } else if (data.cmd === 'list') {
            var routes = {};
            if (routeMap) routeMap.forEach(function (v, k) { routes[k] = v; });
            port.postMessage({ routes: routes });

        } else {
            port.postMessage({ error: 'unknown cmd: ' + data.cmd });
        }
    };
};
