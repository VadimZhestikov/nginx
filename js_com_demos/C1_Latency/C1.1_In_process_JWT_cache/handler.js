// C1.1 — In-process JWT cache backed by SharedArrayBuffer
//
// SAB layout: CACHE_SLOTS slots, each SLOT_SIZE Int32 words
//   [0] hash of token string
//   [1] second hash (reduce collisions)
//   [2] valid flag (1 = occupied)
//   [3] reserved / expiry placeholder
//
// Because the SAB lives in shared memory, all worker processes see
// the same cache data without any IPC round-trip.

var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

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

function b64decode(s) {
    s = s.replace(/-/g, '+').replace(/_/g, '/');
    while (s.length % 4) s += '=';
    var bytes = [], i = 0;
    for (i = 0; i < s.length; i += 4) {
        var c0 = B64.indexOf(s[i]),   c1 = B64.indexOf(s[i + 1]);
        var c2 = s[i + 2] === '=' ? 0 : B64.indexOf(s[i + 2]);
        var c3 = s[i + 3] === '=' ? 0 : B64.indexOf(s[i + 3]);
        bytes.push((c0 << 2) | (c1 >> 4));
        if (s[i + 2] !== '=') bytes.push(((c1 & 15) << 4) | (c2 >> 2));
        if (s[i + 3] !== '=') bytes.push(((c2 & 3) << 6) | c3);
    }
    return String.fromCharCode.apply(null, bytes);
}

// ── SAB-backed cache ──────────────────────────────────────────────────────
var CACHE_SLOTS = 16;
var SLOT_SIZE   = 4;   // 4 × Int32 per slot
var cacheSab    = new SharedArrayBuffer(CACHE_SLOTS * SLOT_SIZE * 4);
var cacheArr    = new Int32Array(cacheSab);

function hashStr(s) {
    var h = 0;
    for (var i = 0; i < s.length; i++) h = (Math.imul(h, 31) + s.charCodeAt(i)) | 0;
    return h;
}

function hashStr2(s) {
    var h = 5381;
    for (var i = 0; i < s.length; i++) h = (Math.imul(h, 33) ^ s.charCodeAt(i)) | 0;
    return h;
}

function cacheGet(token) {
    var h1   = hashStr(token);
    var h2   = hashStr2(token);
    var slot = ((h1 >>> 0) % CACHE_SLOTS) * SLOT_SIZE;
    return Atomics.load(cacheArr, slot + 2) === 1 &&
           Atomics.load(cacheArr, slot)     === h1  &&
           Atomics.load(cacheArr, slot + 1) === h2;
}

function cacheSet(token) {
    var h1   = hashStr(token);
    var h2   = hashStr2(token);
    var slot = ((h1 >>> 0) % CACHE_SLOTS) * SLOT_SIZE;
    Atomics.store(cacheArr, slot,     h1);
    Atomics.store(cacheArr, slot + 1, h2);
    Atomics.store(cacheArr, slot + 2, 1);
}

function cacheCount() {
    var n = 0;
    for (var i = 0; i < CACHE_SLOTS; i++) {
        if (Atomics.load(cacheArr, i * SLOT_SIZE + 2) === 1) n++;
    }
    return n;
}

// ── Fake JWT helpers ──────────────────────────────────────────────────────
// Real HMAC-SHA256 is not available in QuickJS without native modules.
// We generate a "signed" token as header.payload.fakesig so the structure
// looks like a JWT and the demo can still decode the payload.
var SECRET = 'nginx-js-demo-secret';

function makeToken(sub) {
    var header  = b64encode(JSON.stringify({ alg: 'HS256', typ: 'JWT' }));
    var payload = b64encode(JSON.stringify({ sub: sub, iat: Date.now() }));
    // Fake signature: hash of header+payload+secret (not cryptographic)
    var sig = b64encode(String(hashStr(header + '.' + payload + SECRET)));
    return header + '.' + payload + '.' + sig;
}

function verifyToken(token) {
    // Returns parsed payload or null
    var parts = token.split('.');
    if (parts.length !== 3) return null;
    try {
        var payload = JSON.parse(b64decode(parts[1]));
        // Recompute expected sig
        var expectedSig = b64encode(
            String(hashStr(parts[0] + '.' + parts[1] + SECRET))
        );
        if (parts[2] !== expectedSig) return null;
        return payload;
    } catch (e) {
        return null;
    }
}

// ── Wire up locations ─────────────────────────────────────────────────────
(function () {
    var server = nginx.http.servers[0];

    // /admin/token/ — issue a demo JWT
    var tokenLoc = server.findLocation('/admin/token/');
    tokenLoc.handler = function (r) {
        var sub = r.args.match(/(?:^|&)sub=([^&]*)/) ?
                  r.args.match(/(?:^|&)sub=([^&]*)/)[1] : 'demo-user';
        var token = makeToken(sub);
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ token: token }) + '\n');
    };

    // /protected/ — validate JWT; use SAB cache to skip re-verification
    var protectedLoc = server.findLocation('/protected/');
    protectedLoc.handler = function (r) {
        var auth = r.headers['Authorization'] || r.headers['authorization'] || '';
        var token = auth.replace(/^Bearer\s+/, '');
        if (!token) {
            r.respond(401, {}, 'missing token\n');
            return;
        }

        var hit = cacheGet(token);
        if (hit) {
            r.respond(200, { 'X-Cache': 'HIT' }, 'access granted (cache hit)\n');
            return;
        }

        // Cache miss — do the "expensive" verification
        var payload = verifyToken(token);
        if (!payload) {
            r.respond(403, { 'X-Cache': 'MISS' }, 'invalid token\n');
            return;
        }

        cacheSet(token);
        r.respond(200, { 'X-Cache': 'MISS' },
            'access granted for ' + payload.sub + ' (cache miss, now cached)\n');
    };

    // /admin/cache-stats/ — report how many slots are occupied
    var statsLoc = server.findLocation('/admin/cache-stats/');
    statsLoc.handler = function (r) {
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ cached: cacheCount(), slots: CACHE_SLOTS }) + '\n');
    };
}());
