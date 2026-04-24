// D2.1 — Zero Trust Gateway
//
// Every request to /api/* is validated IN-PROCESS against a Bearer token
// before the content handler runs.  No sidecar, no external auth service
// round-trip — the validation executes at nginx speed inside the JS engine.
//
// Token format (demo — NOT cryptographically secure):
//   Base64(JSON.stringify({user, role, exp}))
//
// Validation checks:
//   1. Authorization header present with "Bearer " prefix
//   2. Base64 decode + JSON parse succeeds
//   3. `exp` timestamp is in the future (not expired)
//   4. `role` is in the set of roles allowed for the requested path
//
// /health/      — exempt from auth (liveness probe)
// /api/         — requires any authenticated user
// /api/admin/   — requires role === 'admin'
// /dev/token/   — mint demo tokens for testing
//
// In production replace the base64 token with a real OIDC JWT verified
// by a native C function for HMAC-SHA256 / RS256 signature checking.

(function () {

    // ----------------------------------------------------------------
    // Minimal Base64 encode/decode (QuickJS has no atob/btoa)
    // ----------------------------------------------------------------
    var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

    function b64encode(s) {
        var bytes = [];
        for (var i = 0; i < s.length; i++) {
            bytes.push(s.charCodeAt(i) & 0xff);
        }
        var out = '';
        for (var i = 0; i < bytes.length; i += 3) {
            var b0 = bytes[i], b1 = bytes[i+1] || 0, b2 = bytes[i+2] || 0;
            out += B64[b0 >> 2];
            out += B64[((b0 & 3) << 4) | (b1 >> 4)];
            out += (i + 1 < bytes.length) ? B64[((b1 & 0xf) << 2) | (b2 >> 6)] : '=';
            out += (i + 2 < bytes.length) ? B64[b2 & 0x3f] : '=';
        }
        return out;
    }

    function b64decode(s) {
        s = s.replace(/-/g, '+').replace(/_/g, '/');
        while (s.length % 4) { s += '='; }
        var bytes = [], i = 0;
        while (i < s.length) {
            var c0 = B64.indexOf(s[i++]);
            var c1 = B64.indexOf(s[i++]);
            var c2 = s[i] === '=' ? (i++, 0) : B64.indexOf(s[i++]);
            var c3 = s[i] === '=' ? (i++, 0) : B64.indexOf(s[i++]);
            bytes.push((c0 << 2) | (c1 >> 4));
            if (c2 !== 0 || s[i-2] !== '=') bytes.push(((c1 & 0xf) << 4) | (c2 >> 2));
            if (c3 !== 0 || s[i-1] !== '=') bytes.push(((c2 & 0x3) << 6) | c3);
        }
        return String.fromCharCode.apply(null, bytes);
    }

    // ----------------------------------------------------------------
    // Query string parser (r.args is a raw string, not an object)
    // ----------------------------------------------------------------
    function parseArgs(qs) {
        var out = {};
        (qs || '').split('&').forEach(function (part) {
            var kv = part.split('=');
            if (kv[0]) { out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || ''); }
        });
        return out;
    }

    // ----------------------------------------------------------------
    // Token helpers
    // ----------------------------------------------------------------
    function mintToken(user, role, ttlSeconds) {
        var now = Math.floor(Date.now() / 1000);
        var claims = { user: user, role: role, exp: now + ttlSeconds };
        return b64encode(JSON.stringify(claims));
    }

    function validateToken(authHeader) {
        if (!authHeader || authHeader.indexOf('Bearer ') !== 0) {
            return { ok: false, status: 401, err: 'Missing or invalid Authorization header' };
        }

        var token = authHeader.slice(7).trim();
        var claims;
        try {
            claims = JSON.parse(b64decode(token));
        } catch (e) {
            return { ok: false, status: 401, err: 'Malformed token' };
        }

        var now = Math.floor(Date.now() / 1000);
        if (!claims.exp || claims.exp <= now) {
            return { ok: false, status: 403, err: 'Token expired' };
        }

        if (!claims.user || !claims.role) {
            return { ok: false, status: 401, err: 'Missing claims' };
        }

        return { ok: true, claims: claims };
    }

    // ----------------------------------------------------------------
    // Route wiring
    // ----------------------------------------------------------------
    var server = nginx.http.servers[0];

    function findLoc(path) {
        return server.findLocation(path);
    }

    // /health/ — no auth, liveness probe
    findLoc('/health/').handler = function (r) {
        r.respond(200, {}, 'ok\n');
    };

    // /dev/token/ — mint a demo token (not for production)
    findLoc('/dev/token/').handler = function (r) {
        var args = parseArgs(r.args);
        var user = args['user'] || 'demo';
        var role = args['role'] || 'user';
        var ttl  = parseInt(args['ttl'] || '300', 10);
        var token = mintToken(user, role, ttl);
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({ token: token, user: user, role: role }) + '\n');
    };

    // /api/ — requires valid token (any role)
    var apiLoc = findLoc('/api/');
    apiLoc.addHook(function (req) {
        var result = validateToken(req.headers['authorization']);
        if (!result.ok) {
            req.respond(result.status, {
                'WWW-Authenticate': 'Bearer'
            }, 'Unauthorized: ' + result.err + '\n');
            return;  // returning without calling next() stops the chain
        }
        // Store claims for the content handler
        req.ctx.claims = result.claims;
    });
    apiLoc.handler = function (r) {
        var claims = r.ctx.claims || {};
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            message: 'Access granted',
            user: claims.user,
            role: claims.role
        }) + '\n');
    };

    // /api/admin/ — requires role === 'admin'
    var adminLoc = findLoc('/api/admin/');
    adminLoc.addHook(function (req) {
        var result = validateToken(req.headers['authorization']);
        if (!result.ok) {
            req.respond(result.status, {
                'WWW-Authenticate': 'Bearer'
            }, 'Unauthorized: ' + result.err + '\n');
            return;
        }
        if (result.claims.role !== 'admin') {
            req.respond(403, {}, 'Forbidden: admin role required\n');
            return;
        }
        req.ctx.claims = result.claims;
    });
    adminLoc.handler = function (r) {
        var claims = r.ctx.claims || {};
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            message: 'Admin access granted',
            user: claims.user,
            role: claims.role
        }) + '\n');
    };

})();
