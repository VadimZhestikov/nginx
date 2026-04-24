// D1.3 — Cross-Worker Quota Enforcement
//
// A SharedArrayBuffer implements a lock-free token bucket shared across ALL
// worker processes.  Each /api/ request atomically decrements the counter;
// when it reaches zero every subsequent call returns 429 Too Many Requests —
// regardless of which worker handles the request.
//
// SAB layout (Int32Array):
//   [0] — remaining token count
//   [1] — total requests served (for telemetry)
//   [2] — total requests rejected (for telemetry)
//
// Atomics.add(arr, 0, -1) returns the OLD value before decrement.
// If the old value was > 0 we consumed a token successfully.
// If the old value was <= 0 we immediately restore (+1) and reject.
//
// This avoids any mutex and works correctly under concurrent worker load.

var QUOTA_MAX = 10;

// Allocate pre-fork so every worker maps the same physical memory page
var sab = new SharedArrayBuffer(12);
var arr = new Int32Array(sab);

// Start with a full bucket
Atomics.store(arr, 0, QUOTA_MAX);
Atomics.store(arr, 1, 0);
Atomics.store(arr, 2, 0);

(function () {
    var server = nginx.http.servers[0];

    function findLoc(path) {
        return server.findLocation(path);
    }

    // GET /api/ — consume one token or return 429
    findLoc('/api/').handler = function (r) {
        // Attempt to decrement — returns OLD value
        var oldVal = Atomics.add(arr, 0, -1);

        if (oldVal <= 0) {
            // Quota exhausted — restore the decrement (prevent negative drift)
            Atomics.add(arr, 0, 1);
            Atomics.add(arr, 2, 1);  // rejected counter
            r.respond(429, {
                'X-Quota-Remaining': '0',
                'Retry-After': '1'
            }, 'Too Many Requests — quota exhausted\n');
            return;
        }

        // Token consumed successfully
        var served = Atomics.add(arr, 1, 1) + 1;
        var remaining = oldVal - 1;

        r.respond(200, {
            'X-Quota-Remaining': String(remaining),
            'X-Request-Number': String(served)
        }, 'OK — request #' + served + ', tokens remaining: ' + remaining + '\n');
    };

    // GET /admin/refill/ — reset the token bucket to QUOTA_MAX
    findLoc('/admin/refill/').handler = function (r) {
        Atomics.store(arr, 0, QUOTA_MAX);
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            refilled: true,
            quota: QUOTA_MAX
        }) + '\n');
    };

    // GET /status/ — inspect current quota state
    findLoc('/status/').handler = function (r) {
        var remaining = Atomics.load(arr, 0);
        var served    = Atomics.load(arr, 1);
        var rejected  = Atomics.load(arr, 2);
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            quota_max:       QUOTA_MAX,
            tokens_remaining: remaining < 0 ? 0 : remaining,
            requests_served:  served,
            requests_rejected: rejected
        }, null, 2) + '\n');
    };
})();
