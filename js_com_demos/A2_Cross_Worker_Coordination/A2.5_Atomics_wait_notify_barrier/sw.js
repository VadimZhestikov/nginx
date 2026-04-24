// A2.5 SharedWorker — Atomics barrier participant
//
// Runs a continuous compute loop: waits for signal, computes, stores result.
//
// SAB layout (Int32):
//   [0] state: 0=idle, 1=work-requested, 2=result-ready
//   [1] input:  value to compute
//   [2] result: fibonacci result
//
// The SW thread loops: wait for state==1, compute fib(arr[1]), store result
// in arr[2], set state=2, notify. Then reset to idle and wait again.

onconnect = function (e) {
    var port = e.ports[0];
    var arr  = null;

    // Once we receive the SAB, start the compute loop
    port.onmessage = function (msg) {
        if (msg.data instanceof SharedArrayBuffer) {
            arr = new Int32Array(msg.data);
            port.postMessage('ready');   // acknowledge to worker
            computeLoop();
        }
    };

    function computeLoop() {
        if (!arr) { return; }

        // Iterative loop — avoids stack overflow from tail-recursive calls
        for (;;) {
            // Wait for state to become 1 (work-requested); timeout 1s so we
            // don't spin-burn when idle
            Atomics.wait(arr, 0, 0, 1000);

            // Check if work was actually requested
            if (Atomics.load(arr, 0) === 1) {
                var n      = Atomics.load(arr, 1);
                var result = fib(n);

                Atomics.store(arr, 2, result);  // write result
                Atomics.store(arr, 0, 2);       // signal result-ready
                Atomics.notify(arr, 0, 1);      // wake waiting worker

                // Wait for worker to reset state to 0 before next iteration
                Atomics.wait(arr, 0, 2, 2000);
            }
            // Loop back: wait for next work-request
        }
    }
};

function fib(n) {
    if (n <= 1) { return n; }
    var a = 0, b = 1;
    for (var i = 2; i <= n; i++) {
        var c = a + b;
        a = b;
        b = c;
    }
    return b;
}
