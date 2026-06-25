// A2.9 SharedWorker — global cross-nginx-worker aggregate.
//
// One SW thread in the master, reached over the channel by the worker-0 of
// every nginx worker's local pool. It accumulates the per-request local sums
// into a global total + count that any nginx worker can read. This is the job
// the SharedWorker is actually for: shared state ACROSS worker processes,
// signalled over the channel (never cross-process Atomics).
//
//   {op:'add', value:N}  -> globalTotal += N; globalCount++; reply {globalTotal, globalCount}
//   {op:'get'}           -> reply {globalTotal, globalCount}

var globalTotal = 0;
var globalCount = 0;

onconnect = function (e) {
    var port = e.ports[0];
    port.onmessage = function (msg) {
        var d = msg.data || {};
        if (d.op === 'add') {
            globalTotal += d.value;
            globalCount += 1;
        }
        port.postMessage({ globalTotal: globalTotal, globalCount: globalCount });
    };
};
