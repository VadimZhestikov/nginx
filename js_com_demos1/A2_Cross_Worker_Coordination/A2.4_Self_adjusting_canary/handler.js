// A2.4 — Self-Adjusting Canary
//
// A JS-controlled canary deployment:
//   - Start with canaryWeight = 10% of traffic goes to the canary version.
//   - Each 5xx from the canary increments an error counter.
//   - When errors exceed a threshold, canaryWeight automatically drops to 0%.
//   - Admin can manually adjust weight or reset error count.
//
// In a real deployment the backends would be nginx upstreams proxied via
// r.subrequest(). Here they are simulated as in-process handlers so the
// demo is self-contained (no external servers required).

(function () {
    var locs = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // State (worker_processes 1 — use SAB for multi-worker)
    var canaryWeight = 10;   // percent (0–100)
    var canaryErrors = 0;
    var totalRequests = 0;
    var ERROR_THRESHOLD = 3;  // auto-kill canary after this many errors

    // Simulated stable backend (always 200)
    findLoc('/backend/stable/').handler = function (r) {
        r.respond(200, {}, 'stable-v1\n');
    };

    // Simulated canary backend (fails if ?fail=1 to demonstrate error tracking)
    findLoc('/backend/canary/').handler = function (r) {
        if (r.args === 'fail') {
            r.respond(500, {}, 'canary-error\n');
            return;
        }
        r.respond(200, {}, 'canary-v2\n');
    };

    // GET /api/ — route traffic to stable or canary based on weight
    findLoc('/api/').handler = async function (r) {
        totalRequests++;
        var routeToCanary = (canaryWeight > 0) && (Math.random() * 100 < canaryWeight);

        var path     = routeToCanary ? '/backend/canary/' : '/backend/stable/';
        var failArg  = r.args === 'fail' && routeToCanary ? '?fail=1' : '';
        var result   = await r.subrequest(path + failArg.replace('?', ''));

        // Track canary errors
        if (routeToCanary && result.status >= 500) {
            canaryErrors++;
            nginx.log(3, 'Canary error #' + canaryErrors + ' (threshold=' + ERROR_THRESHOLD + ')');

            if (canaryErrors >= ERROR_THRESHOLD) {
                nginx.log(3, 'Canary error threshold reached — auto-killing canary (weight 0%)');
                canaryWeight = 0;
            }
        }

        r.respond(result.status, {
            'X-Routed-To': routeToCanary ? 'canary' : 'stable',
            'X-Canary-Weight': canaryWeight + '%',
        }, result.body || '');
    };

    // GET /admin/status/ — show routing state
    findLoc('/admin/status/').handler = function (r) {
        r.respond(200, {}, JSON.stringify({
            canaryWeight:   canaryWeight,
            canaryErrors:   canaryErrors,
            errorThreshold: ERROR_THRESHOLD,
            totalRequests:  totalRequests,
            canaryLive:     canaryWeight > 0,
        }) + '\n');
    };

    // POST /admin/weight/?<percent> — manually set canary weight (0–100)
    findLoc('/admin/weight/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }
        var w = parseInt(r.args, 10);
        if (isNaN(w) || w < 0 || w > 100) {
            r.respond(400, {}, 'weight must be 0–100\n');
            return;
        }
        canaryWeight = w;
        canaryErrors = 0;  // reset error count when weight is set manually
        nginx.log(4, 'Canary weight set to ' + w + '%');
        r.respond(200, {}, 'canary-weight=' + w + '%\n');
    };

    // POST /admin/report-error/ — simulate external error report
    findLoc('/admin/report-error/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }
        canaryErrors++;
        if (canaryErrors >= ERROR_THRESHOLD) {
            canaryWeight = 0;
        }
        r.respond(200, {}, 'errors=' + canaryErrors + ' weight=' + canaryWeight + '%\n');
    };
})();
