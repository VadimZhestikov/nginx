// A2.1 — Global In-Memory Rate Counter
//
// A SharedArrayBuffer is allocated before fork so it has the SAME virtual
// address in every worker process (and the master).  Atomics.add() increments
// the counter atomically — no mutex needed, no external store required.
//
// With worker_processes 2 both workers share the same physical memory page,
// so /count/ always reflects the true total regardless of which worker handles
// the /hit/ requests.
//
// SAB layout (Int32, 4 bytes per element):
//   [0] — total hit counter
//   [1] — last worker index that serviced a /hit/ request

// Allocate the SAB at config-phase (before fork) — same VA in all workers
var sab    = new SharedArrayBuffer(8);
var arr    = new Int32Array(sab);

(function () {
    var locs = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // GET /hit/ — increment the global counter atomically
    findLoc('/hit/').handler = function (r) {
        var newCount = Atomics.add(arr, 0, 1) + 1;   // add returns OLD value
        Atomics.store(arr, 1, nginx.workerIdx);
        r.respond(200, {}, 'hit #' + newCount + ' (worker ' + nginx.workerIdx + ')\n');
    };

    // GET /count/ — read the global counter (no lock needed for a single read)
    findLoc('/count/').handler = function (r) {
        var total     = Atomics.load(arr, 0);
        var lastWorker = Atomics.load(arr, 1);
        r.respond(200, {}, 'total=' + total + ' last-worker=' + lastWorker + '\n');
    };

    // POST /reset/ — reset counter to zero
    findLoc('/reset/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }
        Atomics.store(arr, 0, 0);
        Atomics.store(arr, 1, -1);
        r.respond(200, {}, 'reset\n');
    };
})();
