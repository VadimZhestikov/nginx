// A2.5 — SharedWorker compute offload (channel signal + SAB data plane)
//
// An nginx worker offloads a CPU computation (Fibonacci) to a SharedWorker
// thread WITHOUT blocking the event loop.  Two shared resources, each used for
// what it is actually good at:
//
//   * SharedArrayBuffer  — the DATA plane.  Input and result live in shared
//     memory (a pre-fork SAB, same physical page in every process), so no
//     payload is copied across the process boundary.
//   * SharedWorker channel (postMessage/onmessage) — the SIGNAL plane.  The
//     channel is an AF_UNIX socket the nginx event loop already watches via
//     epoll, so a reply wakes the worker immediately and reliably.
//
// Why not Atomics.wait/notify for the signal?  This demo used to.  But the
// SharedWorker lives in the master process and nginx workers are forked
// children, so the signalling was CROSS-PROCESS — and QuickJS implements
// Atomics.wait/notify with a PER-PROCESS waiter list (a pthread mutex + cond
// vars), not OS futexes.  A notify() from a worker process therefore never
// wakes the SW thread parked in the master: cross-process notify is inert.
// The old code masked this by making the SW busy-poll a SAB flag on a 10 ms
// Atomics.wait timeout, which (a) is decorative w.r.t. notify and (b) can be
// starved for SECONDS under CPU contention, because the scheduler gets no
// signal that work is waiting.  The channel does not have this problem: the
// message delivery itself makes the SW thread runnable.
//
// SAB data is still shared by reference; the channel just says "go / done".

import * as os from 'os';

var cwd    = os.getcwd()[0] || '.';
var swPath = cwd + '/sw.js';

// Pre-fork SAB — same VA/physical page in master and workers (allocated before
// fork).  arr[0] = input n, arr[1] = result.  Tiny here, but the point is the
// pattern: for a LARGE payload this is a zero-copy hand-off.
var sab = new SharedArrayBuffer(8);   // 2 x Int32
var arr = new Int32Array(sab);

(function () {
    var locs = nginx.http.servers[0].locations;
    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    var lastResult = null;
    var lastInput  = null;

    // One round-trip: write input to the SAB, open a channel to the SW, ask it
    // to compute, and resolve when its reply lands on the channel (epoll-driven,
    // no polling).  A fresh SharedWorker handle per call gives each request its
    // own reply port, so replies never need correlating.
    function compute(n) {
        return new Promise(function (resolve) {
            Atomics.store(arr, 0, n);            // input -> shared data plane
            var ch = new SharedWorker(swPath);
            ch.onmessage = function () {         // SIGNAL: result is in arr[1]
                resolve(Atomics.load(arr, 1));
            };
            ch.postMessage(sab);                 // SIGNAL: "compute now" (+ SAB ref)
        });
    }

    // One shared SAB (single input/result slot) => one computation at a time.
    // Serialise requests through a promise chain so concurrent /compute/ calls
    // can't clobber each other's slots.  (This serialisation is the cost of a
    // SHARED data plane; passing the payload in the message instead would be
    // naturally concurrent — the trade-off SAB-vs-message makes explicit.)
    var chain = Promise.resolve();

    // GET /compute/?n=<number> — offload fibonacci(n) to the SW
    findLoc('/compute/').handler = function (r) {
        var n = parseInt(r.args, 10);
        if (isNaN(n) || n < 0 || n > 40) {
            r.respond(400, {}, 'n must be 0-40\n');
            return;
        }
        var run = function () {
            return compute(n).then(function (result) {
                lastResult = result;
                lastInput  = n;
                r.respond(200, {}, 'fib(' + n + ')=' + result + '\n');
            });
        };
        chain = chain.then(run, run);
        return chain;
    };

    // GET /status/ — show last computation
    findLoc('/status/').handler = function (r) {
        if (lastResult === null) {
            r.respond(200, {}, 'no computation yet\n');
            return;
        }
        r.respond(200, {}, 'last: fib(' + lastInput + ')=' + lastResult + '\n');
    };
})();
