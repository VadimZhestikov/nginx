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
var sab = new SharedArrayBuffer(16);   // 4 x Int32
var arr = new Int32Array(sab);
// arr[0] = 0  (state: 0=idle, 1=work-requested, 2=result-ready)
// arr[1] = 0  (input)
// arr[2] = 0  (result)
// arr[3] = 0  (stop flag: 1 = SW should exit its loop)

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

    // Send SAB to SW and wait for 'ready' confirmation in each worker
    nginx.broadcast(function () {
        sw.onmessage = function (e) {
            if (e.data === 'ready') {
                nginx.log(5, 'A2.5: SW compute loop is ready');
            }
        };
        sw.postMessage(sab);
    });

    var lastResult = null;
    var lastInput  = null;

    // Non-blocking poll: wait until arr[0] becomes targetVal
    function waitFor(targetVal, timeoutMs) {
        return new Promise(function (resolve, reject) {
            var elapsed  = 0;
            var interval = 10;   // poll every 10ms

            function poll() {
                if (Atomics.load(arr, 0) === targetVal) {
                    resolve();
                    return;
                }
                elapsed += interval;
                if (elapsed >= timeoutMs) {
                    reject(new Error('timeout waiting for arr[0]=' + targetVal));
                    return;
                }
                nginx.setTimeout(interval).then(poll);
            }

            poll();
        });
    }

    // GET /compute/?n=<number> — offload fibonacci(n) to the SW
    findLoc('/compute/').handler = async function (r) {
        var n = parseInt(r.args, 10);
        if (isNaN(n) || n < 0 || n > 40) {
            r.respond(400, {}, 'n must be 0-40\n');
            return;
        }

        // Set up SAB for this computation (state must be idle before we write)
        Atomics.store(arr, 1, n);   // write input
        Atomics.store(arr, 2, 0);   // clear previous result
        Atomics.store(arr, 0, 1);   // set state = work-requested
        Atomics.notify(arr, 0, 1);  // wake the SW

        // Non-blocking poll for completion (arr[0] == 2)
        try {
            await waitFor(2, 3000);
        } catch (err) {
            // Reset state so SW can handle next request
            Atomics.store(arr, 0, 0);
            r.respond(504, {}, 'timeout: SW did not respond\n');
            return;
        }

        var result = Atomics.load(arr, 2);
        // Reset state to idle for next request
        Atomics.store(arr, 0, 0);
        Atomics.notify(arr, 0, 1);   // wake SW so it loops back to wait

        lastResult = result;
        lastInput  = n;

        r.respond(200, {}, 'fib(' + n + ')=' + result + '\n');
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
        Atomics.store(arr, 3, 1);   // set stop flag
        Atomics.notify(arr, 0, 1);  // wake SW so it sees the flag immediately
        r.respond(200, {}, 'SW stop signal sent\n');
    };
})();
