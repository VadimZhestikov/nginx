// A2.3 — Persistent Background Thread
//
// Demonstrates a persistent background "ticker" using nginx.setTimeout()
// within a nginx.broadcast() callback. The ticker runs in each worker and
// stores its value in nginx.shared for cross-worker visibility.
//
// This is the worker-side equivalent of a background thread: an async
// recursive timer loop that runs independently of request handling.
//
// For a true OS-thread-based background task (separate pthread), see the
// SharedWorker demos (A2.5, js_shared_worker.t).

(function () {
    var locs = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Start the background ticker in each worker after startup
    nginx.broadcast(function () {
        // Seed initial values
        if (nginx.shared.get('bg.tick') === undefined) {
            nginx.shared.set('bg.tick',  '0');
            nginx.shared.set('bg.value', '0');
        }

        // Background ticker: increments every 500ms using nginx.setTimeout
        function tick() {
            var t = parseInt(nginx.shared.get('bg.tick')  || '0', 10) + 1;
            var v = t * 7;
            nginx.shared.set('bg.tick',  String(t));
            nginx.shared.set('bg.value', String(v));
            nginx.setTimeout(500).then(tick);
        }

        // Start the ticker loop (only in worker 0 to avoid double-increment)
        if (nginx.workerIdx === 0) {
            nginx.setTimeout(500).then(tick);
        }
    });

    // GET /value/ — return current tick and computed value
    findLoc('/value/').handler = function (r) {
        var tick  = nginx.shared.get('bg.tick')  || '0';
        var value = nginx.shared.get('bg.value') || '0';
        r.respond(200, {}, JSON.stringify({ tick: parseInt(tick, 10), value: parseInt(value, 10) }) + '\n');
    };

    // POST /tick/ — manually trigger one tick
    findLoc('/tick/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }
        var t = parseInt(nginx.shared.get('bg.tick')  || '0', 10) + 1;
        var v = t * 7;
        nginx.shared.set('bg.tick',  String(t));
        nginx.shared.set('bg.value', String(v));
        r.respond(200, {}, JSON.stringify({ tick: t, value: v }) + '\n');
    };

    // POST /reset/ — reset counters to zero
    findLoc('/reset/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }
        nginx.shared.set('bg.tick',  '0');
        nginx.shared.set('bg.value', '0');
        r.respond(200, {}, JSON.stringify({ tick: 0, value: 0 }) + '\n');
    };
})();
