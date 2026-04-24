// A1.4 — Config Snapshot & Rollback
//
// Demonstrates loc.snapshot() / snap.restore():
//   1. GET /data/           → returns current response (starts as "v1-stable")
//   2. POST /admin/snapshot/ → saves current state
//   3. POST /admin/change/   → modifies /data/ handler to return "v2-experimental"
//   4. GET /data/            → now returns "v2-experimental"
//   5. POST /admin/rollback/ → restores the snapshot
//   6. GET /data/            → returns "v1-stable" again
//
// The snapshot captures the location's handler and all settable config fields.
// restore() is atomic — a single call reverts the location to its saved state.

(function () {
    var locs = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Mutable state
    var currentVersion = 'v1-stable';
    var savedSnapshot  = null;
    var snapshotLabel  = null;

    // /data/ — the endpoint whose behaviour we will mutate and roll back
    var dataLoc = findLoc('/data/');
    dataLoc.handler = function (r) {
        r.respond(200, {}, currentVersion + '\n');
    };

    // POST /admin/snapshot/ — save current state
    findLoc('/admin/snapshot/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

        savedSnapshot = dataLoc.snapshot();
        snapshotLabel = currentVersion;
        r.respond(200, {}, 'snapshot saved: ' + snapshotLabel + '\n');
    };

    // POST /admin/change/?label=<name> — mutate /data/ handler
    findLoc('/admin/change/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

        var label = r.args || 'v2-experimental';
        currentVersion = label;

        // Re-install handler with new version string
        dataLoc.handler = function (req) {
            req.respond(200, {}, currentVersion + '\n');
        };

        r.respond(200, {}, 'changed to: ' + currentVersion + '\n');
    };

    // POST /admin/rollback/ — restore to saved snapshot
    findLoc('/admin/rollback/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

        if (!savedSnapshot) {
            r.respond(409, {}, 'no snapshot saved\n');
            return;
        }

        currentVersion = snapshotLabel;
        savedSnapshot.restore();

        r.respond(200, {}, 'rolled back to: ' + snapshotLabel + '\n');
    };

    // GET /admin/status/ — current version and snapshot info
    findLoc('/admin/status/').handler = function (r) {
        r.respond(200, {}, JSON.stringify({
            current:  currentVersion,
            snapshot: snapshotLabel || null
        }) + '\n');
    };
})();
