/*
 * metrics plugin
 *
 * Installs a global HTTP hook that increments per-location request
 * counters in nginx.shared.  Counters are keyed as:
 *   metrics:<location_path>:requests
 *
 * Example nginx.shared entries after traffic:
 *   metrics:/:requests          → "42"
 *   metrics:/api/:requests      → "7"
 *   metrics:/admin/ws:requests  → "3"
 */
(function() {
    var knownLocations = ['/', '/admin/', '/admin/ws', '/info',
                          '/health', '/web/', '/api/', '/cache/'];
    var _seeded = false;

    nginx.http.addHook(function(req) {
        /* Seed counters at zero on first request per worker so they
         * appear in the Shared State panel before any traffic arrives. */
        if (!_seeded) {
            _seeded = true;
            knownLocations.forEach(function(path) {
                var key = 'metrics:' + path + ':requests';
                if (nginx.shared.get(key) === undefined) {
                    nginx.shared.set(key, '0');
                }
            });
        }

        var key = 'metrics:' + req.uri.replace(/\?.*$/, '') + ':requests';
        nginx.shared.incr(key);
    });

    nginx.log(6, 'metrics plugin: request counters initialised');
}());
