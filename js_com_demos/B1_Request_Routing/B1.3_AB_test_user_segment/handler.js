// B1.3 — A/B Test User Segment Routing
//
// Routes visitors to different landing page variants based on two conditions:
//   1. Cookie "segment" — explicit opt-in to beta (variant B)
//   2. Time of day — after 18:00 all non-beta users see "landing A evening"
//
// Classic nginx: the $cookie_<name> variable gives the cookie value, and the
// $time_iso8601 / $time_local variables provide the time — but combining them
// in routing logic requires a complex chain of map{} blocks.  The "hour"
// cannot be extracted without Lua or a custom variable.  In JavaScript, it is
// three lines of standard Date arithmetic.

(function () {
    var servers = nginx.http.servers;

    var server = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/landing/'; });
    });

    server.locations.find(function (l) { return l.path === '/landing/'; })
        .handler = function (r) {
            var cookies = {};
            var cookieHeader = r.headers['cookie'] || '';
            cookieHeader.split(';').forEach(function (part) {
                var kv = part.trim().split('=');
                if (kv.length >= 2) {
                    cookies[kv[0].trim()] = kv.slice(1).join('=').trim();
                }
            });

            var segment = cookies['segment'] || '';
            var hour = new Date().getHours();  // server local time, 0-23

            var variant;
            if (segment === 'beta') {
                variant = 'landing B';
            } else if (hour >= 18) {
                variant = 'landing A evening';
            } else {
                variant = 'landing A';
            }

            r.respond(200, {'X-Variant': variant}, variant + '\n');
        };
})();
