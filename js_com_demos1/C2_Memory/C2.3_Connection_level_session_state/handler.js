// C2.3 — Per-request scratchpad via r.ctx
//
// r.ctx is a plain JS object tied to the lifetime of a single request.
// Hooks and the handler all share the same r.ctx, allowing them to pass
// data without global variables.  When the request ends, r.ctx is GC'd.
//
// This demo shows three pipeline stages communicating through r.ctx:
//   1. addHook (before handler) — records start time + request ID
//   2. handler                  — does "work", records duration, reads ctx
//   3. X-Ctx-* response headers — populated from r.ctx fields
//
// /status/ — returns r.ctx as JSON + adds X-Request-Id and X-Duration headers
// /echo/   — demonstrates r.ctx surviving through an async await

(function () {
    var server = nginx.http.servers[0];
    var requestCounter = 0;

    // ── /status/ ──────────────────────────────────────────────────────────

    var statusLoc = server.findLocation('/status/');

    // Hook: runs before the handler; populates r.ctx
    statusLoc.addHook(function (r, next) {
        r.ctx.startTime  = Date.now();
        r.ctx.requestId  = 'req-' + (++requestCounter);
        r.ctx.workerPid  = String(nginx.workerPid || 0);
        r.ctx.hookRan    = true;
        next(r);
    });

    // Handler: reads r.ctx, records duration, responds
    statusLoc.handler = function (r) {
        var duration = Date.now() - r.ctx.startTime;
        r.ctx.duration   = duration;
        r.ctx.handlerRan = true;

        r.respond(200,
            {
                'Content-Type': 'application/json',
                'X-Request-Id': r.ctx.requestId,
                'X-Duration':   String(duration) + 'ms'
            },
            JSON.stringify({
                requestId:  r.ctx.requestId,
                startTime:  r.ctx.startTime,
                duration:   r.ctx.duration,
                hookRan:    r.ctx.hookRan,
                handlerRan: r.ctx.handlerRan,
                workerPid:  r.ctx.workerPid
            }, null, 2) + '\n'
        );
    };

    // ── /echo/ ────────────────────────────────────────────────────────────

    var echoLoc = server.findLocation('/echo/');

    echoLoc.addHook(function (r, next) {
        r.ctx.phase    = 'hook';
        r.ctx.hookTime = Date.now();
        next(r);
    });

    echoLoc.handler = async function (r) {
        // r.ctx survives across await points
        r.ctx.phase = 'before-await';

        // Simulate async work (subrequest to self would also work)
        await new Promise(function (resolve) { resolve(); });

        r.ctx.phase       = 'after-await';
        r.ctx.handlerTime = Date.now();

        var elapsed = r.ctx.handlerTime - r.ctx.hookTime;

        r.respond(200,
            {
                'Content-Type':  'application/json',
                'X-Ctx-Phase':   r.ctx.phase,
                'X-Ctx-Elapsed': elapsed + 'ms'
            },
            JSON.stringify({
                phase:       r.ctx.phase,
                hookTime:    r.ctx.hookTime,
                handlerTime: r.ctx.handlerTime,
                elapsedMs:   elapsed,
                survived:    'r.ctx persisted through async await'
            }, null, 2) + '\n'
        );
    };
}());
