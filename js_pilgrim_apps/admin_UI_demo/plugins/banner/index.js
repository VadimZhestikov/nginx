/*
 * banner plugin
 *
 * Installs a response hook on every location that appends two
 * informational headers to each response:
 *
 *   X-Powered-By:  nginx-js-pilgrim/<version>
 *   X-Request-Id:  <monotonically incrementing counter>
 *
 * The request ID is stored in nginx.shared so it survives across
 * requests on different worker processes.
 */
(function() {
    var VERSION      = nginx.version || 'dev';
    var _initialized = false;

    function bannerHook(req) {
        /* Initialise the counter on first response per worker. */
        if (!_initialized) {
            _initialized = true;
            if (nginx.shared.get('banner:request_id') === undefined) {
                nginx.shared.set('banner:request_id', '0');
            }
        }

        req.setHeader('X-Powered-By', 'nginx-js-pilgrim/' + VERSION);

        var id = nginx.shared.incr('banner:request_id');
        req.setHeader('X-Request-Id', String(id));
    }

    /* Install on every location across all servers */
    var servers = nginx.http.servers;
    for (var si = 0; si < servers.length; si++) {
        var locs = servers[si].locations;
        for (var li = 0; li < locs.length; li++) {
            locs[li].addResponseHook(bannerHook);
        }
    }

    nginx.log(6, 'banner plugin: response headers enabled');
}());
