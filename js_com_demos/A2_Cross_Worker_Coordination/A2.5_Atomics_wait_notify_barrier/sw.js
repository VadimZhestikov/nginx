// A2.5 SharedWorker — Atomics barrier participant
//
// SAB layout (Int32, 16 bytes = 4 slots):
//   [0] state: 0=idle, 1=work-requested, 2=result-ready
//   [1] input:  fibonacci argument
//   [2] result: fibonacci result
//   [3] stop:   0=running, 1=terminate (set by /stop-sw/ before nginx -s stop)
//
// Without arr[3]: the infinite Atomics.wait loop blocks the pthread forever,
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
        for (;;) {
            // Short timeout (500 ms) so we can check the stop flag promptly.
            Atomics.wait(arr, 0, 0, 500);

            // Termination signal: worker set arr[3]=1 before nginx -s stop.
            if (Atomics.load(arr, 3) === 1) { break; }

            if (Atomics.load(arr, 0) === 1) {
                var result = fib(Atomics.load(arr, 1));
                Atomics.store(arr, 2, result);
                Atomics.store(arr, 0, 2);       // signal result-ready
                Atomics.notify(arr, 0, 1);      // wake waiting worker

                // Wait for worker to acknowledge (reset state to 0).
                Atomics.wait(arr, 0, 2, 500);
                if (Atomics.load(arr, 3) === 1) { break; }
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
