// B2.1 — Inline JWT Validation
//
// Decodes and validates a JWT-like token entirely inside nginx — no external
// auth service round-trip.  The token is three base64url-encoded segments:
//   header.claims.signature
//
// Validation steps performed:
//   1. Structural check (exactly three dot-separated segments)
//   2. Base64url-decode the claims segment
//   3. JSON-parse the claims
//   4. Check that `sub` (subject) is present
//   5. Check that `exp` (expiry) is in the future
//
// NOTE: Signature verification is intentionally omitted — QuickJS does not
// ship with a crypto library.  A real deployment would call a native C
// function for HMAC-SHA256 verification.  This demo isolates and shows the
// structural/expiry logic that lives in pure JavaScript.

(function () {
    // -------------------------------------------------------------------------
    // Base64url helpers (QuickJS has no atob/btoa)
    // -------------------------------------------------------------------------
    var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

    function b64decode(s) {
        s = s.replace(/-/g, '+').replace(/_/g, '/');
        while (s.length % 4) s += '=';
        var bytes = [], i = 0;
        while (i < s.length) {
            var c0 = B64.indexOf(s[i]);
            var c1 = B64.indexOf(s[i + 1]);
            var ch2 = s[i + 2];
            var ch3 = s[i + 3];
            var c2 = ch2 === '=' ? 0 : B64.indexOf(ch2);
            var c3 = ch3 === '=' ? 0 : B64.indexOf(ch3);
            i += 4;
            bytes.push((c0 << 2) | (c1 >> 4));
            if (ch2 !== '=') bytes.push(((c1 & 0xf) << 4) | (c2 >> 2));
            if (ch3 !== '=') bytes.push(((c2 & 0x3) << 6) | c3);
        }
        return String.fromCharCode.apply(null, bytes);
    }

    function b64encode(s) {
        var bytes = [];
        for (var i = 0; i < s.length; i++) bytes.push(s.charCodeAt(i) & 0xff);
        var out = '', b = 0, bits = 0;
        for (var i = 0; i < bytes.length; i++) {
            b = (b << 8) | bytes[i];
            bits += 8;
            while (bits >= 6) { out += B64[(b >> (bits - 6)) & 63]; bits -= 6; }
        }
        if (bits > 0) out += B64[(b << (6 - bits)) & 63];
        while (out.length % 4) out += '=';
        return out.replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '');
    }

    // -------------------------------------------------------------------------
    // Token generation (demo-only, no real HMAC)
    // -------------------------------------------------------------------------
    function makeToken(sub, ttlSeconds) {
        var header = b64encode(JSON.stringify({alg: 'HS256', typ: 'JWT'}));
        var now    = Math.floor(Date.now() / 1000);
        var claims = b64encode(JSON.stringify({
            sub: sub,
            iat: now,
            exp: now + ttlSeconds
        }));
        // Fake signature — NOT cryptographically valid
        var sig = b64encode('demo-signature');
        return header + '.' + claims + '.' + sig;
    }

    // -------------------------------------------------------------------------
    // Token validation
    // -------------------------------------------------------------------------
    function validateToken(token) {
        if (!token) return {ok: false, err: 'missing token'};

        var parts = token.split('.');
        if (parts.length !== 3) return {ok: false, err: 'malformed token'};

        var claims;
        try {
            claims = JSON.parse(b64decode(parts[1]));
        } catch (e) {
            return {ok: false, err: 'invalid claims JSON'};
        }

        if (!claims.sub) return {ok: false, err: 'missing sub'};

        var now = Math.floor(Date.now() / 1000);
        if (!claims.exp || claims.exp <= now) {
            return {ok: false, err: 'token expired'};
        }

        return {ok: true, claims: claims};
    }

    // -------------------------------------------------------------------------
    // Route setup
    // -------------------------------------------------------------------------
    var servers = nginx.http.servers;
    var server = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/admin/token/'; });
    });
    var locs = server.locations;

    // Parse a query string into an object
    function parseArgs(qs) {
        var out = {};
        (qs || '').split('&').forEach(function (part) {
            var kv = part.split('=');
            if (kv[0]) out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || '');
        });
        return out;
    }

    // GET /admin/token/?sub=alice&ttl=60  — issues a demo token
    locs.find(function (l) { return l.path === '/admin/token/'; })
        .handler = function (r) {
            var args = parseArgs(r.args);
            var sub = args['sub'] || 'demo-user';
            var ttl = parseInt(args['ttl'] || '300', 10);
            var token = makeToken(sub, ttl);
            r.respond(200, {'Content-Type': 'text/plain'}, token + '\n');
        };

    // GET /protected/  — validate Bearer token
    locs.find(function (l) { return l.path === '/protected/'; })
        .handler = function (r) {
            var auth = r.headers['authorization'] || '';
            var token = '';
            if (auth.indexOf('Bearer ') === 0) {
                token = auth.slice(7).trim();
            }

            var result = validateToken(token);
            if (!result.ok) {
                r.respond(401, {'WWW-Authenticate': 'Bearer'},
                    'Unauthorized: ' + result.err + '\n');
                return;
            }

            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify({
                    message: 'access granted',
                    sub:     result.claims.sub,
                    exp:     result.claims.exp
                }) + '\n');
        };
})();
