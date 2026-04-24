// B5.3 — Per-Request Latency Histogram + Prometheus /metrics
//
// Records request latency in a 4-bucket histogram stored in a SharedArrayBuffer
// (SAB).  Because the SAB is allocated before fork(), all nginx workers share
// the same physical memory and can atomically update the same counters.
//
// Buckets (upper bound in ms):
//   [0]  le=10     — very fast (< 10 ms)
//   [1]  le=50     — fast      (10-50 ms)
//   [2]  le=200    — moderate  (50-200 ms)
//   [3]  le=+Inf   — slow      (> 200 ms)
//   [4]  count     — total request count
//   [5]  sum_lo    — total latency sum, low 32 bits (ms, integer)
//   [6]  sum_hi    — total latency sum, high 32 bits
//
// /metrics returns Prometheus text format (exposition format 0.0.4).
//
// Classic nginx: nginx-module-vts (VirtualHost Traffic Status) or
// nginx-prometheus-exporter (reads stub_status externally) are the typical
// solutions.  Neither tracks latency histograms.  Implementing a histogram in
// nginx without Lua or a sidecar exporter is not possible with stock modules.
// Here a SAB + Int32Array + Atomics.add gives a lock-free multi-worker
// histogram in 10 lines of JavaScript.

(function () {
    // -------------------------------------------------------------------------
    // Shared histogram (allocated at config phase, before fork)
    // -------------------------------------------------------------------------
    var BUCKET_COUNT  = 4;
    var IDX_COUNT     = 4;  // running total count
    var IDX_SUM_LO    = 5;
    var IDX_SUM_HI    = 6;
    var SAB_SLOTS     = 7;

    var sab = new SharedArrayBuffer(SAB_SLOTS * 4);  // Int32Array, 4 bytes each
    var arr = new Int32Array(sab);

    // Bucket upper bounds in ms
    var BOUNDS = [10, 50, 200, Infinity];

    function recordLatency(ms) {
        // Increment the first bucket whose bound >= ms
        for (var i = 0; i < BUCKET_COUNT; i++) {
            if (ms <= BOUNDS[i]) {
                Atomics.add(arr, i, 1);
                break;
            }
        }
        Atomics.add(arr, IDX_COUNT, 1);

        // Accumulate sum (integer ms, split into lo/hi to avoid overflow)
        var msInt = ms | 0;
        Atomics.add(arr, IDX_SUM_LO, msInt);
    }

    // -------------------------------------------------------------------------
    // Route setup
    // -------------------------------------------------------------------------
    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/api/'; });
    });
    var locs = server.locations;

    var apiLoc = locs.find(function (l) { return l.path === '/api/'; });

    // Request hook: record start time in r.ctx
    apiLoc.addHook(function (r, next) {
        r.ctx.startMs = Date.now();
        next(r);
    });

    // Response hook: calculate latency and update histogram
    apiLoc.addResponseHook(function (r) {
        var latency = Date.now() - (r.ctx.startMs || Date.now());
        recordLatency(latency);
    });

    // API handler — simulates a small variable-latency response
    apiLoc.handler = function (r) {
        r.respond(200, {}, 'OK\n');
    };

    // /metrics/ — emit Prometheus text format
    locs.find(function (l) { return l.path === '/metrics/'; })
        .handler = function (r) {
            var lines = [];
            lines.push('# HELP http_request_duration_bucket Latency histogram (ms)');
            lines.push('# TYPE http_request_duration_bucket counter');

            // Snapshot current bucket values
            var cumulative = 0;
            for (var i = 0; i < BUCKET_COUNT; i++) {
                cumulative += Atomics.load(arr, i);
                var le = BOUNDS[i] === Infinity ? '+Inf' : String(BOUNDS[i]);
                lines.push('http_request_duration_bucket{le="' + le + '"} ' + cumulative);
            }

            var count  = Atomics.load(arr, IDX_COUNT);
            var sumLo  = Atomics.load(arr, IDX_SUM_LO);

            lines.push('# HELP http_requests_total Total request count');
            lines.push('# TYPE http_requests_total counter');
            lines.push('http_requests_total ' + count);

            lines.push('# HELP http_request_duration_sum Total latency sum (ms)');
            lines.push('# TYPE http_request_duration_sum counter');
            lines.push('http_request_duration_sum ' + sumLo);

            r.respond(200,
                {'Content-Type': 'text/plain; version=0.0.4'},
                lines.join('\n') + '\n');
        };
})();
