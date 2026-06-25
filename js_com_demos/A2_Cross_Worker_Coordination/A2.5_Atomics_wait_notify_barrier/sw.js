// A2.5 SharedWorker — compute participant (channel-signalled, stateless)
//
// SAB layout (Int32, 8 bytes = 2 slots):
//   [0] input   (fibonacci argument, written by the worker)
//   [1] result  (fibonacci result, written here by the SW)
//
// The channel is the signal: the worker postMessages the SAB ("compute now"),
// the SW reads the input from shared memory, writes the result back to shared
// memory, and postMessages a one-word reply ("done").  No Atomics.wait, no
// busy-poll, no infinite loop — the SW thread is event-driven, parked in its
// channel recv until a message arrives, so it is never CPU-starved and the
// master can shut it down cleanly (no terminate flag needed).
//
// Atomics.load/store are used purely for well-defined concurrent access to the
// shared slots; there is no Atomics.wait/notify anywhere (cross-process notify
// is inert — see handler.js).

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (msg) {
        if (msg.data instanceof SharedArrayBuffer) {
            var arr = new Int32Array(msg.data);
            var n   = Atomics.load(arr, 0);      // input from shared data plane
            Atomics.store(arr, 1, fib(n));       // result -> shared data plane
            port.postMessage('done');            // signal: result is ready in arr[1]
        }
    };
};

function fib(n) {
    if (n <= 1) { return n; }
    var a = 0, b = 1;
    for (var i = 2; i <= n; i++) { var c = a + b; a = b; b = c; }
    return b;
}
