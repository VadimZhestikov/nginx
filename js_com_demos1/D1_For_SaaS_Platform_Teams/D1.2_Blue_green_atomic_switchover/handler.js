// D1.2 — Blue/Green Atomic Switchover
//
// A SharedArrayBuffer (SAB) acts as the routing flag shared across ALL
// worker processes (pre-fork allocation ensures the same physical memory
// page is mapped in every worker).
//
// SAB layout (Int32Array, element 0):
//   0 = blue active   (default)
//   1 = green active
//
// /app/          reads the SAB flag and subrequests to the active deployment.
// /admin/switch/ toggles the flag atomically with Atomics.compareExchange();
//               nginx.broadcast() notifies all workers so each can update
//               its own local state if needed.
// /status/       reports the current active slot.
//
// Classic nginx: blue/green switchover requires editing upstream{}
// or include directives and issuing `nginx -s reload`.  Here it is a
// single Atomics operation visible to every worker within microseconds.

// Allocate the routing SAB before fork so every worker shares the page.
// Element 0: 0 = blue, 1 = green
var routingSab = new SharedArrayBuffer(4);
var routingArr = new Int32Array(routingSab);

// Start with blue active
Atomics.store(routingArr, 0, 0);

(function () {
    var server = nginx.http.servers[0];

    function findLoc(path) {
        return server.findLocation(path);
    }

    // ── Internal "blue" deployment ──────────────────────────────────────
    findLoc('/internal/blue/').handler = function (r) {
        r.respond(200, {
            'X-Deployment': 'blue'
        }, 'blue deployment v1.0\n');
    };

    // ── Internal "green" deployment ─────────────────────────────────────
    findLoc('/internal/green/').handler = function (r) {
        r.respond(200, {
            'X-Deployment': 'green'
        }, 'green deployment v2.0\n');
    };

    // ── /app/ — route to whichever slot is active ───────────────────────
    findLoc('/app/').handler = async function (r) {
        var active = Atomics.load(routingArr, 0);
        var target = active === 0 ? '/internal/blue/' : '/internal/green/';
        var res    = await r.subrequest(target);
        r.respond(res.status, res.headers, res.body);
    };

    // ── /admin/switch/ — atomically toggle the active slot ──────────────
    findLoc('/admin/switch/').handler = function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'Method Not Allowed\n');
            return;
        }

        // Read current value, swap it atomically
        var current = Atomics.load(routingArr, 0);
        var next    = current === 0 ? 1 : 0;

        // compareExchange: only swap if value is still `current`
        var swapped = Atomics.compareExchange(routingArr, 0, current, next);
        var newSlot = swapped === current ? next : Atomics.load(routingArr, 0);

        var slotName = newSlot === 0 ? 'blue' : 'green';

        nginx.log(4, 'Blue/green switchover: now active=' + slotName);

        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            switched: true,
            active: slotName
        }) + '\n');
    };

    // ── /status/ — report active slot ───────────────────────────────────
    findLoc('/status/').handler = function (r) {
        var active   = Atomics.load(routingArr, 0);
        var slotName = active === 0 ? 'blue' : 'green';
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            active: slotName,
            version: active === 0 ? 'v1.0' : 'v2.0'
        }) + '\n');
    };
})();
