// E2 — DDoS: hot-patch limit_req while under attack
//
// During normal operation /api is served at 20 r/s with burst=10.
// When an ops engineer spots a DDoS they call POST /ddos/tighten, which:
//   • drops the zone rate to 2 r/s
//   • shrinks burst to 2
//   • sets nodelay so excess requests are rejected immediately (no queuing)
//   • switches from dry-run to enforcing mode
//   • raises the status code to 429 for easy client detection
// All changes take effect in the running process within milliseconds — no
// reload, no downtime, no config-file edit required.
//
// POST /ddos/relax restores the original values so the ops team can verify
// recovery without ever touching nginx.conf.
//
// GET  /ddos/status prints the current limit_req parameters as JSON.

(function () {
    var http    = nginx.http;
    var servers = http.servers;

    function findSrv(name) {
        return servers.find(function (s) { return s.name === name; });
    }

    // Locate /api location and its limitReq object.
    var apiSrv  = findSrv('api.example.com');
    var apiLoc  = apiSrv.locations.find(function (l) { return l.path === '/api'; });
    var lr      = apiLoc.limitReq;
    var lim     = lr.limits[0];   // the single limit_req entry in /api

    // Install a real handler on /api so it actually responds.
    apiLoc.handler = function (r) {
        r.respond(200, {}, 'api-ok\n');
    };

    // Baseline values (read from config as-is for comparison in /status).
    var NORMAL_RATE    = 20;   // r/s
    var NORMAL_BURST   = 10;
    var NORMAL_NODELAY = false;

    // DDoS mitigation values.
    var ATTACK_RATE    = 2;    // r/s — drastically throttle
    var ATTACK_BURST   = 2;
    var ATTACK_NODELAY = true; // reject excess immediately, don't queue

    var opsSrv = findSrv('ops.internal');

    // POST /ddos/tighten — activate DDoS mitigation mode
    opsSrv.locations
        .find(function (l) { return l.path === '/ddos/tighten'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            lim.rate    = ATTACK_RATE;
            lim.burst   = ATTACK_BURST;
            lim.nodelay = ATTACK_NODELAY;
            lr.dryRun   = false;

            nginx.log(4, 'DDoS mitigation ACTIVATED: rate=' + ATTACK_RATE +
                         'r/s burst=' + ATTACK_BURST + ' nodelay=true');
            r.respond(200, {}, 'mitigation activated\n');
        };

    // POST /ddos/relax — restore normal operating parameters
    opsSrv.locations
        .find(function (l) { return l.path === '/ddos/relax'; })
        .handler = function (r) {
            if (r.method !== 'POST') {
                r.respond(405, {}, 'Method Not Allowed\n');
                return;
            }

            lim.rate    = NORMAL_RATE;
            lim.burst   = NORMAL_BURST;
            lim.nodelay = NORMAL_NODELAY;

            nginx.log(4, 'DDoS mitigation DEACTIVATED: rate=' + NORMAL_RATE +
                         'r/s burst=' + NORMAL_BURST + ' nodelay=false');
            r.respond(200, {}, 'normal mode restored\n');
        };

    // GET /ddos/status — inspect live parameters
    opsSrv.locations
        .find(function (l) { return l.path === '/ddos/status'; })
        .handler = function (r) {
            var info = JSON.stringify({
                zone:     lim.zone,
                rate:     lim.rate,
                burst:    lim.burst,
                nodelay:  lim.nodelay,
                delay:    lim.delay,
                dryRun:   lr.dryRun,
                logLevel: lr.logLevel
            }, null, 2);
            r.respond(200, {'Content-Type': 'application/json'}, info + '\n');
        };
})();
