// A2.5 SharedWorker — Atomics barrier participant
//
// SAB layout (Int32, 24 bytes = 6 slots):
//   [0] reqSeq:  monotonic request id (worker increments per job)
//   [1] input:   fibonacci argument
//   [2] result:  fibonacci result for reqSeq
//   [3] doneSeq: reqSeq the SW has finished (worker polls until >= its seq)
//   [4] stop:    0=running, 1=terminate (set by /stop-sw/ before nginx -s stop)
//
// Monotonic sequences instead of a 0/1/2 state machine + ack: the SW blocks
// while reqSeq is unchanged, computes when it advances, and publishes doneSeq.
// There is no ack to get out of phase with, so consecutive requests can't
// desync (the bug that made every other /compute/ time out).
//
// Without arr[4]: the infinite Atomics.wait loop blocks the pthread forever,
// preventing the master process from joining it on shutdown (visible as a
// hung process in futex_wait even after nginx -s stop).

onconnect = function (e) {
    var port = e.ports[0];
    var arr  = null;

    port.onmessage = function (msg) {
        if (msg.data instanceof SharedArrayBuffer) {
            arr = new Int32Array(msg.data);
            port.postMessage('ready');
            computeLoop();
        }
    };

    function computeLoop() {
        var lastSeq = 0;

        for (;;) {
            // Block while reqSeq is unchanged.  The worker's Atomics.notify is
            // the fast wakeup path, BUT a cross-process notify is best-effort
            // here — it does not reliably wake this SharedWorker pthread (the
            // futex notify from the worker process is frequently missed).  So
            // the short timeout is the RELIABLE path: re-check reqSeq every
            // 10 ms.  (A long timeout strands requests that land between ticks,
            // which is what made every other /compute/ time out.)  Idle cost is
            // ~100 trivial wakeups/sec on a dedicated thread — negligible.
            Atomics.wait(arr, 0, lastSeq, 10);

            if (Atomics.load(arr, 4) === 1) { break; }   // terminate

            var seq = Atomics.load(arr, 0);
            if (seq !== lastSeq) {                        // new work published
                var result = fib(Atomics.load(arr, 1));
                Atomics.store(arr, 2, result);
                Atomics.store(arr, 3, seq);               // publish doneSeq
                Atomics.notify(arr, 3, 1);                // (worker polls, but be nice)
                lastSeq = seq;
            }
        }
    }
};

function fib(n) {
    if (n <= 1) { return n; }
    var a = 0, b = 1;
    for (var i = 2; i <= n; i++) { var c = a + b; a = b; b = c; }
    return b;
}
