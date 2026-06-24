// A2.5 — Atomics Wait/Notify Barrier
//
// Demonstrates cross-process synchronisation using SharedArrayBuffer +
// Atomics between an nginx worker and a SharedWorker thread.
//
// Architecture:
//   - A pre-fork SAB (same VA in master and workers) is used for signalling.
//   - A static SharedWorker (created at config phase) runs a compute loop:
//     it waits on arr[0], computes fib(arr[1]), stores result in arr[2].
//   - Workers write the input and signal the SW via Atomics.notify.
//   - Workers poll for completion using nginx.setTimeout (non-blocking).
//
// Note: Atomics.wait() in the nginx worker event loop thread would block the
// entire event loop — never do that.  The SW thread (separate pthread) safely
// uses Atomics.wait().  Workers poll with nginx.setTimeout(10) instead.

import * as os from 'os';

// Pre-fork SAB — same VA in master and all workers (allocated before fork).
// Futex shared mode works cross-process because the physical page is the same.
//
// Protocol uses MONOTONIC sequence numbers rather than a 0/1/2 state machine
// with a worker->SW ack.  The old ack handshake coupled consecutive requests:
// once timing slipped by one phase, every other request waited on a transition
// that had already happened and timed out (504).  With monotonic counters the
// worker bumps reqSeq for each job and waits for doneSeq to catch up — a lost
// or late wakeup self-heals on the SW's next (timed) wait; nothing wedges.
var sab = new SharedArrayBuffer(24);   // 6 x Int32
var arr = new Int32Array(sab);
// arr[0] = reqSeq  (worker increments once per request; SW waits for a change)
// arr[1] = input   (fibonacci argument for the current request)
// arr[2] = result  (fibonacci result for reqSeq)
// arr[3] = doneSeq (SW sets to the reqSeq it just finished)
// arr[4] = stop    (1 = SW should exit its loop)

(function () {
    // Resolve absolute path for the SW script
    var cwd    = os.getcwd()[0] || '.';
    var swPath = cwd + '/sw.js';
    var locs   = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Create static SharedWorker at config phase (master process)
    var sw = new SharedWorker(swPath);

    // Per-worker flag: set once this worker has seen the SW's 'ready' reply
    // (SW received the SAB and its compute loop is running).
    var swReady = false;

    nginx.broadcast(function () {
        sw.onmessage = function (e) {
            if (e.data === 'ready') {
                swReady = true;
                nginx.log(5, 'A2.5: SW compute loop is ready');
            }
        };
        sw.postMessage(sab);
    });

    // Non-blocking wait for the SW 'ready' handshake (startup only).
    function waitForReady(timeoutMs) {
        return new Promise(function (resolve, reject) {
            var elapsed = 0, interval = 5;
            (function poll() {
                if (swReady) { resolve(); return; }
                elapsed += interval;
                if (elapsed >= timeoutMs) { reject(new Error('SW not ready')); return; }
                nginx.setTimeout(interval).then(poll);
            })();
        });
    }

    var lastResult = null;
    var lastInput  = null;

    // Non-blocking poll: wait until the SW's doneSeq reaches our request seq.
    // (The worker event-loop thread must never Atomics.wait — it would block
    // every connection — so we poll with nginx.setTimeout instead.)
    function waitForDone(seq, timeoutMs) {
        return new Promise(function (resolve, reject) {
            var elapsed  = 0;
            var interval = 5;   // poll every 5ms

            function poll() {
                if (Atomics.load(arr, 3) >= seq) {   // doneSeq caught up
                    resolve();
                    return;
                }
                elapsed += interval;
                if (elapsed >= timeoutMs) {
                    reject(new Error('timeout waiting for doneSeq>=' + seq));
                    return;
                }
                nginx.setTimeout(interval).then(poll);
            }

            poll();
        });
    }

    // One shared SAB + one SW => one computation at a time.  Serialise requests
    // through a promise chain so concurrent /compute/ calls can't clobber each
    // other's input/result slots (and so a request only reads the result that
    // belongs to its own seq).
    var chain = Promise.resolve();

    async function doCompute(r, n) {
        // Don't race SW startup: ensure its compute loop is running first.
        if (!swReady) {
            try { await waitForReady(5000); }
            catch (e) { r.respond(503, {}, 'SW not ready\n'); return; }
        }

        // Submit the job, then poll for our doneSeq.  Retry with a fresh seq on
        // timeout: immediately after startup the very first job can occasionally
        // not reach the SW (a one-shot cross-process SAB/wakeup warmup hiccup);
        // a re-submit against the now-warm SW always lands.  Each attempt uses
        // the same n, so a late result from a prior attempt is still correct.
        for (var attempt = 0; attempt < 5; attempt++) {
            Atomics.store(arr, 1, n);               // input BEFORE bumping reqSeq
            var seq = Atomics.add(arr, 0, 1) + 1;   // unique increasing request id
            Atomics.notify(arr, 0, 1);              // wake the SW from its wait
            try {
                await waitForDone(seq, 1000);
            } catch (e) {
                continue;   // re-submit
            }
            var result = Atomics.load(arr, 2);
            lastResult = result;
            lastInput  = n;
            r.respond(200, {}, 'fib(' + n + ')=' + result + '\n');
            return;
        }
        r.respond(504, {}, 'timeout: SW did not respond\n');
    }

    // GET /compute/?n=<number> — offload fibonacci(n) to the SW
    findLoc('/compute/').handler = function (r) {
        var n = parseInt(r.args, 10);
        if (isNaN(n) || n < 0 || n > 40) {
            r.respond(400, {}, 'n must be 0-40\n');
            return;
        }
        // queue behind any in-flight computation (success or failure)
        var run = function () { return doCompute(r, n); };
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

    // GET /stop-sw/ — signal the SharedWorker to exit its compute loop.
    // Must be called before nginx -s stop; otherwise the pthread blocks in
    // Atomics.wait and the master process hangs until the OS kills it.
    findLoc('/stop-sw/').handler = function (r) {
        Atomics.store(arr, 4, 1);   // set stop flag
        Atomics.notify(arr, 0, 1);  // wake SW so it sees the flag immediately
        r.respond(200, {}, 'SW stop signal sent\n');
    };
})();
