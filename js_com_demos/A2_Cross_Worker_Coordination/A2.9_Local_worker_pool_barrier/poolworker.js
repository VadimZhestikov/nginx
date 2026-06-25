// A2.9 pool worker — one of POOL local JS Workers inside an nginx worker process.
//
// Each worker sums its slice of [0, n) into the shared SAB, then meets the
// others at a SENSE-REVERSING BARRIER built from Atomics.wait/notify. Because
// all POOL workers live in the SAME process, intra-process notify reliably
// wakes a parked worker — this is exactly the case Atomics is for. (Contrast
// A2.5: its SW lived in the master, so cross-process notify was inert.)
//
// After the barrier every partial is visible. Worker 0 — and only worker 0 —
// holds the link to the SharedWorker (in the master) and talks to it over the
// CHANNEL (the cross-process hop never uses Atomics). Routing all SW traffic
// through one thread per nginx worker avoids colliding on the single per-worker
// SW channel slot.
//
// Job dispatch arrives as a normal postMessage and this handler RETURNS after
// each message, so the worker is always parked in its channel loop between jobs
// and stays terminable (a worker stuck in an infinite Atomics.wait could not be
// joined on shutdown).

var id, POOL, arr, sw = null;
var BARRIER = 0, SENSE = 1, PART = 2;

var localSense = 0;     // per-worker barrier sense, flips every barrier()
var pendingLocal = 0;   // worker 0: local sum awaiting the SharedWorker reply

function sliceSum(n) {
    var lo = Math.floor(id * n / POOL);
    var hi = Math.floor((id + 1) * n / POOL);
    var s = 0;
    for (var k = lo; k < hi; k++) { s += k; }
    return s;
}

// Sense-reversing barrier: reusable across jobs with no reset race. The last
// arriver resets the counter, flips the shared sense, and notifies; everyone
// else parks in Atomics.wait until the sense matches their local sense.
function barrier() {
    localSense ^= 1;
    var arrived = Atomics.add(arr, BARRIER, 1) + 1;
    if (arrived === POOL) {
        Atomics.store(arr, BARRIER, 0);
        Atomics.store(arr, SENSE, localSense);
        Atomics.notify(arr, SENSE);
    } else {
        var v;
        while ((v = Atomics.load(arr, SENSE)) !== localSense) {
            Atomics.wait(arr, SENSE, v);   // intra-process: notify wakes us reliably
        }
    }
}

onmessage = function (e) {
    var m = e.data;

    if (m.type === 'init') {
        id   = m.id;
        POOL = m.pool;
        arr  = new Int32Array(m.sab);
        if (id === 0) {
            // Worker 0 owns the (only) link to the SharedWorker for this nginx
            // worker. Keep it persistent so the connection outlives any single
            // op and is not GC'd between the send and the reply.
            sw = new SharedWorker(m.swPath);
            sw.onmessage = function (rep) {
                postMessage({ type: 'result',
                              local:  pendingLocal,
                              global: rep.data.globalTotal,
                              count:  rep.data.globalCount });
            };
        }
        postMessage({ type: 'ready' });
        return;
    }

    if (m.type === 'job') {
        Atomics.store(arr, PART + id, sliceSum(m.n));   // map: my slice -> SAB
        barrier();                                       // <- the Atomics showcase

        if (id === 0) {
            var total = 0;
            for (var j = 0; j < POOL; j++) { total += Atomics.load(arr, PART + j); }
            pendingLocal = total;
            sw.postMessage({ op: 'add', value: total });  // worker -> SharedWorker
            // the {type:'result'} reply is posted from sw.onmessage above
        }
        return;
    }

    if (m.type === 'query') {     // worker 0 only: read the global aggregate
        pendingLocal = 0;
        sw.postMessage({ op: 'get' });
        return;
    }
};
