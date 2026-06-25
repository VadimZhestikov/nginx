// A2.9 — Local JS Worker pool with an intra-process Atomics barrier,
//        plus a cross-worker aggregate in a SharedWorker.
//
// This is the POSITIVE counterpart to A2.5: it shows where Atomics.wait/notify
// genuinely belongs. The rule the demo embodies:
//
//   * Atomics.wait/notify  — SAME-PROCESS coordination only. A pool of local
//     JS Workers (pthreads inside THIS nginx worker process) shares a SAB and
//     synchronises with a real Atomics barrier. Intra-process notify reliably
//     wakes a parked thread, so this is correct and efficient (no busy-poll).
//   * the channel (postMessage) — anything crossing the worker<->master
//     boundary. Job dispatch to the pool rides the channel (so workers stay
//     reachable for terminate()), and the pool's worker 0 — and ONLY worker 0 —
//     talks to the SharedWorker (in the master) to update a GLOBAL aggregate.
//   * SAB — the shared data plane (per-worker partials + barrier counter/sense).
//
// NOTE on the SharedWorker channel: a static SW has exactly one channel slot
// per nginx worker process (nchannels = worker_processes). So within one nginx
// worker, only ONE thread may hold the connection — otherwise the event loop
// and a JS Worker collide on the same socketpair. Here worker 0 owns it; the
// event loop never talks to the SW directly (it routes reads through worker 0).
//
// Flow of GET /task/?n=N (parallel sum of 0..N-1, folded into the global total):
//   1. event loop postMessages the job to all POOL local workers
//   2. each worker sums its slice into the SAB, then meets at an Atomics barrier
//   3. after the barrier, worker 0 reduces the partials -> local sum, sends it
//      to the SharedWorker (channel), and reports {local, global} back
//   4. event loop responds

import * as os from 'os';

var cwd    = os.getcwd()[0] || '.';
var wkPath = cwd + '/poolworker.js';
var swPath = cwd + '/sw.js';

var POOL = 4;

// SAB slot layout (Int32): [0]=barrier counter, [1]=sense, [2..2+POOL-1]=partials
var BARRIER = 0, SENSE = 1, PART = 2;

// ── per-nginx-worker pool (Workers are process-local; each nginx worker builds
//    its own pool lazily on first request) ─────────────────────────────────────
var pool       = null;   // Array<Worker>
var poolReady  = null;   // Promise, resolves once all workers have acked 'ready'
var curResolve = null;   // resolver for the in-flight op's worker-0 reply

function initPool() {
    if (poolReady) return poolReady;
    poolReady = new Promise(function (resolve) {
        var sab = new SharedArrayBuffer((PART + POOL) * 4);
        var arr = new Int32Array(sab);
        Atomics.store(arr, BARRIER, 0);
        Atomics.store(arr, SENSE, 0);

        pool = [];
        var readies = 0;
        for (var i = 0; i < POOL; i++) {
            var w = new Worker(wkPath);
            w.onmessage = function (e) {
                var m = e.data;
                if (m.type === 'ready') {
                    if (++readies === POOL) resolve();
                } else if (m.type === 'result') {
                    var f = curResolve; curResolve = null;
                    if (f) f(m);
                }
            };
            // worker 0 owns the SharedWorker link (swPath); others don't need it
            w.postMessage({ type: 'init', id: i, pool: POOL, sab: sab, swPath: swPath });
            pool.push(w);
        }
    });
    return poolReady;
}

// Send one control message to worker 0 and await its single {type:'result'}.
function ask(msg) {
    return initPool().then(function () {
        return new Promise(function (resolve) {
            curResolve = resolve;
            if (msg.type === 'job') {
                for (var i = 0; i < POOL; i++) pool[i].postMessage(msg);  // all workers
            } else {
                pool[0].postMessage(msg);                                  // worker 0 only
            }
        });
    });
}

// One shared pool + one SAB => one op at a time (serialise everything).
var chain = Promise.resolve();
function serialize(fn) { var run = fn; chain = chain.then(run, run); return chain; }

(function () {
    var server = nginx.http.servers[0];

    // GET /task/?n=N — parallel sum of 0..N-1 via the pool, folded into global
    server.findLocation('/task/').handler = function (r) {
        var n = parseInt(r.args, 10);
        if (isNaN(n) || n < 0 || n > 40000) {
            r.respond(400, {}, 'n must be 0-40000\n');
            return;
        }
        return serialize(function () {
            return ask({ type: 'job', n: n }).then(function (res) {
                r.respond(200, { 'Content-Type': 'application/json' },
                    JSON.stringify({
                        n: n,
                        local_sum:    res.local,
                        global_total: res.global,
                        global_count: res.count
                    }) + '\n');
            });
        });
    };

    // GET /global/ — read the SharedWorker aggregate, routed through worker 0
    server.findLocation('/global/').handler = function (r) {
        return serialize(function () {
            return ask({ type: 'query' }).then(function (res) {
                r.respond(200, { 'Content-Type': 'application/json' },
                    JSON.stringify({ globalTotal: res.global, globalCount: res.count }) + '\n');
            });
        });
    };
})();
