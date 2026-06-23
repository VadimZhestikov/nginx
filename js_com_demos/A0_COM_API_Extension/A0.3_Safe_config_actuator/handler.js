// A0.3 — Safe-Config Actuator
//
// Layer 2 in action: a thin REST actuator over nginx.safeConfig.  Every change
// goes through the safety gateway, which consults nginx.describe() (Layer 1),
// enforces the per-tier opt-in, and fans worker-local writes out to all four
// workers automatically.  Operators call vetted operations; they never touch a
// raw COM path or have to know which writes need fan-out.
//
// Endpoints:
//   GET  /plan?path=&value=   dry-run: class / propagation / would-fan-out
//   POST /canary?pct=N        shift N% of `zoned` traffic to the canary peer
//   POST /header?key=&value=  add a response header to /app (fanned out)
//   POST /toggle?on=0|1       disable (503) / re-enable /app (fanned out)
//   POST /drain?addr=         mark a `zoned` peer down
//   GET  /gate-demo           guarded write rejected (no ack) then accepted
//   GET  /weights             responding worker pid + live zoned peer weights
//   GET  /worker              responding worker pid
//   GET  /app                 app route (carries the header; 503 when toggled off)

// Load the standalone safe-config plugin (resolved relative to the prefix), and
// hand it the cfgworker path so worker-local writes fan out to every worker.
var PLUGIN = '../../../js_com_apps/safe_config';
nginx.use(PLUGIN, { cfgWorkerPath:
    nginx.cycle.prefix + '../../../js_com_apps/safe_config/cfgworker.js' });

nginx.broadcast(function () {
    var sc   = nginx.safeConfig;
    var locs = nginx.http.servers[0].locations;

    function at(path, fn) {
        var l = locs.find(function (l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }
    function pid(r) { return r.variable('pid'); }
    function json(r, code, obj) {
        r.respond(code, {'Content-Type': 'application/json'},
                  JSON.stringify(obj) + '\n');
    }
    function ok(r, obj)  { obj.worker = pid(r); json(r, 200, obj); }
    function err(r, e)   { json(r, 400, { error: String(e.message || e),
                                          worker: pid(r) }); }

    at('/worker',  function (r) { ok(r, {}); });

    at('/weights', function (r) {
        var p = nginx.http.upstreams[0].peers;
        ok(r, { w0: p[0].weight, w1: p[1].weight,
                down0: p[0].down, down1: p[1].down });
    });

    at('/plan', function (r) {
        try {
            var path = r.queryParams.path;
            var val  = JSON.parse(r.queryParams.value || 'null');
            ok(r, { plan: sc.plan(path, val) });
        } catch (e) { err(r, e); }
    });

    at('/canary', function (r) {
        try {
            var pct = parseInt(r.queryParams.pct || '0', 10);
            ok(r, { result: sc.canaryWeight('zoned', pct) });
        } catch (e) { err(r, e); }
    });

    at('/header', function (r) {
        try {
            var res = sc.setResponseHeader('localhost', '/app',
                          r.queryParams.key, r.queryParams.value);
            ok(r, { result: res });
        } catch (e) { err(r, e); }
    });

    at('/toggle', function (r) {
        try {
            var on = r.queryParams.on === '1';
            ok(r, { result: sc.toggleLocation('localhost', '/app', on,
                                               { ack: true }) });
        } catch (e) { err(r, e); }
    });

    at('/drain', function (r) {
        try {
            ok(r, { result: sc.drainPeer('zoned', r.queryParams.addr) });
        } catch (e) { err(r, e); }
    });

    // Shows the gate doing its job: the same guarded write is rejected without
    // an acknowledgement, then accepted with {ack:true}.
    at('/gate-demo', function (r) {
        var path = 'http.servers[0].locations[0].proxy.pass';
        var out  = {};
        try { sc.apply(path, 'http://zoned'); out.withoutAck = 'APPLIED (unexpected)'; }
        catch (e) { out.withoutAck = 'rejected: ' + e.message; }
        try { out.withAck = sc.apply(path, 'http://zoned', { ack: true }); }
        catch (e) { out.withAck = 'rejected: ' + e.message; }
        ok(r, out);
    });

    at('/app', function (r) { r.respond(200, {}, 'app\n'); });
});
