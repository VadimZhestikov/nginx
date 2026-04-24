// C3.2 — Canary deployment with automatic rollback
//
// Monitors 5xx error rates using nginx.shared (cross-worker dictionary).
// When the canary error rate exceeds 40% over a sliding window of 20
// samples, the mode flips to 'rollback' and all traffic goes to stable.
//
// nginx.shared key layout:
//   'canary.mode'     — 'stable' | 'canary' | 'rollback'
//   'canary.history'  — JSON array of recent 5xx booleans (last 20)
//
// Backends (internal locations):
//   /internal/stable/ — always 200
//   /internal/canary/ — always 500 (simulated buggy canary)

var WINDOW_SIZE     = 20;
var ERROR_THRESHOLD = 0.4;  // 40%

function initShared() {
    if (!nginx.shared.get('canary.mode')) {
        nginx.shared.set('canary.mode',    'stable');
        nginx.shared.set('canary.history', '[]');
    }
}

function getMode()    { return nginx.shared.get('canary.mode') || 'stable'; }
function getHistory() {
    try { return JSON.parse(nginx.shared.get('canary.history') || '[]'); }
    catch (e) { return []; }
}

function recordStatus(status) {
    var history = getHistory();
    history.push(status >= 500);
    if (history.length > WINDOW_SIZE) history.shift();
    nginx.shared.set('canary.history', JSON.stringify(history));

    var mode = getMode();
    if (mode === 'canary') {
        var errors = history.filter(function (e) { return e; }).length;
        if (history.length > 0 && errors / history.length > ERROR_THRESHOLD) {
            nginx.shared.set('canary.mode', 'rollback');
        }
    }
}

function errorRate() {
    var h = getHistory();
    if (h.length === 0) return 0;
    var errors = h.filter(function (e) { return e; }).length;
    return errors / h.length;
}

(function () {
    var locs     = nginx.http.servers[0].locations;
    var reqCount = 0;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Initialise nginx.shared state in each worker after startup
    nginx.broadcast(function () {
        initShared();
    });

    // ── Internal backends ──────────────────────────────────────────────────

    findLoc('/internal/stable/').handler = function (r) {
        r.respond(200, { 'X-Backend': 'stable', 'Content-Type': 'application/json' },
            JSON.stringify({ backend: 'stable', ok: true }) + '\n');
    };

    findLoc('/internal/canary/').handler = function (r) {
        r.respond(500, { 'X-Backend': 'canary', 'Content-Type': 'application/json' },
            JSON.stringify({ backend: 'canary', error: 'internal error' }) + '\n');
    };

    // ── Traffic router ─────────────────────────────────────────────────────
    // Route to stable or canary based on current mode; record result.

    findLoc('/canary/').handler = async function (r) {
        var mode      = getMode();
        var useCanary = (mode === 'canary') && ((++reqCount % 2) === 0);
        var target    = useCanary ? '/internal/canary/' : '/internal/stable/';

        var upstream = await r.subrequest(target);
        recordStatus(upstream.status);

        // Build response body with routing metadata
        var upBody;
        try { upBody = JSON.parse(upstream.body); } catch (e) { upBody = {}; }
        upBody.canaryMode   = mode;
        upBody.routedTo     = useCanary ? 'canary' : 'stable';
        upBody.currentMode  = getMode();

        r.respond(upstream.status,
            { 'Content-Type': 'application/json' },
            JSON.stringify(upBody) + '\n');
    };

    // ── Admin endpoints ────────────────────────────────────────────────────

    findLoc('/admin/enable-canary/').handler = function (r) {
        nginx.shared.set('canary.mode',    'canary');
        nginx.shared.set('canary.history', '[]');
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ ok: true, mode: 'canary' }) + '\n');
    };

    findLoc('/admin/disable-canary/').handler = function (r) {
        nginx.shared.set('canary.mode', 'rollback');
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ ok: true, mode: 'rollback' }) + '\n');
    };

    findLoc('/admin/canary-status/').handler = function (r) {
        var history = getHistory();
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({
                mode:      getMode(),
                errorRate: errorRate(),
                samples:   history.length,
                threshold: ERROR_THRESHOLD
            }, null, 2) + '\n');
    };
}());
