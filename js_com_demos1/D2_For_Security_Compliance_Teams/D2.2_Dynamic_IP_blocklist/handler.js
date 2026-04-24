// D2.2 — Dynamic IP Blocklist
//
// nginx.shared provides a cross-worker key/value store backed by shared
// memory.  This demo uses it as an IP blocklist:
//
//   nginx.shared.set('block:10.0.0.99', '1')  — add to blocklist
//   nginx.shared.delete('block:10.0.0.99')    — remove from blocklist
//   nginx.shared.get('block:10.0.0.99')       — check (truthy = blocked)
//
// A location hook on /api/ checks the X-Real-IP header (simulating what
// nginx.realip_module provides in production) against the shared blocklist.
// The check is a single nginx.shared.get() call — no external round-trip,
// no SharedWorker needed, latency < 2 µs.
//
// POST /admin/block/    { "ip": "10.0.0.99" }  — add to blocklist
// POST /admin/unblock/  { "ip": "10.0.0.99" }  — remove from blocklist
// GET  /admin/blocklist/                         — list all blocked IPs
// GET  /api/                                     — gated by IP hook

(function () {
    var server = nginx.http.servers[0];

    var BLOCK_PREFIX = 'block:';

    // ── /api/ — check X-Real-IP against the shared blocklist ────────────
    var apiLoc = server.findLocation('/api/');

    apiLoc.addHook(function (r) {
        var ip = r.headers['x-real-ip'] || r.headers['x-forwarded-for'] || '';
        if (!ip) {
            return;  // no IP header — let the request through
        }

        var blocked = nginx.shared.get(BLOCK_PREFIX + ip);
        if (blocked) {
            nginx.log(4, 'IP blocklist: blocked request from ' + ip);
            r.respond(403, {
                'X-Blocked-IP': ip
            }, 'Forbidden: your IP address is blocked\n');
            // returning without calling next stops the chain
        }
    });

    apiLoc.handler = function (r) {
        var ip = r.headers['x-real-ip'] || r.headers['x-forwarded-for'] || '(no IP header)';
        r.respond(200, {}, 'OK — request from ' + ip + '\n');
    };

    // ── /admin/block/ — add an IP to the blocklist ──────────────────────
    server.findLocation('/admin/block/').handler = async function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'Method Not Allowed\n');
            return;
        }

        var body = await r.readBody();
        var data;
        try { data = JSON.parse(body); } catch (e) {
            r.respond(400, {}, 'Invalid JSON\n');
            return;
        }

        var ip = (data.ip || '').trim();
        if (!ip) {
            r.respond(400, {}, 'Missing "ip" field\n');
            return;
        }

        nginx.shared.set(BLOCK_PREFIX + ip, '1');
        nginx.log(4, 'IP blocklist: added ' + ip);

        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({ blocked: ip }) + '\n');
    };

    // ── /admin/unblock/ — remove an IP from the blocklist ───────────────
    server.findLocation('/admin/unblock/').handler = async function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'Method Not Allowed\n');
            return;
        }

        var body = await r.readBody();
        var data;
        try { data = JSON.parse(body); } catch (e) {
            r.respond(400, {}, 'Invalid JSON\n');
            return;
        }

        var ip = (data.ip || '').trim();
        if (!ip) {
            r.respond(400, {}, 'Missing "ip" field\n');
            return;
        }

        var removed = nginx.shared.delete(BLOCK_PREFIX + ip);
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({ unblocked: ip, was_blocked: removed }) + '\n');
    };

    // ── /admin/blocklist/ — enumerate all blocked IPs ───────────────────
    server.findLocation('/admin/blocklist/').handler = function (r) {
        var keys = nginx.shared.keys();
        var ips  = keys
            .filter(function (k) { return k.indexOf(BLOCK_PREFIX) === 0; })
            .map(function (k) { return k.slice(BLOCK_PREFIX.length); });

        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            count: ips.length,
            blocked_ips: ips
        }, null, 2) + '\n');
    };

})();
