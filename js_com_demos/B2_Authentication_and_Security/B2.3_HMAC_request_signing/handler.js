// B2.3 — HMAC Request Signing
//
// Generates an HMAC-like request signature and shows what Authorization
// header would be added to the proxied upstream request.
//
// Real HMAC-SHA256 requires crypto primitives not available in pure QuickJS.
// This demo implements a deterministic pseudo-HMAC using a polynomial rolling
// hash to illustrate the structure of AWS Signature V4 / API signing patterns:
//
//   1. Build a canonical string: METHOD + \n + URI + \n + timestamp
//   2. Sign it with a secret key using the hash function
//   3. Add Authorization: Hmac-SHA256 Credential=<key-id>,Signature=<hex>
//
// In production, a native C binding would supply real SHA256; the JavaScript
// code shown here (canonical string construction, header assembly, key-id
// lookup) would remain unchanged.

(function () {
    var SECRET_KEY    = 'demo-secret-key-2026';
    var KEY_ID        = 'AKIAIOSFODNN7EXAMPLE';

    // -------------------------------------------------------------------------
    // Pseudo-HMAC: deterministic, NOT cryptographically secure
    // -------------------------------------------------------------------------
    function pseudoHmac(key, message) {
        // djb2-inspired polynomial hash, seeded by key bytes
        var seed = 5381;
        for (var i = 0; i < key.length; i++) {
            seed = ((seed << 5) + seed) ^ key.charCodeAt(i);
            seed = seed >>> 0;
        }

        var h = seed;
        for (var i = 0; i < message.length; i++) {
            h = ((h << 5) + h) ^ message.charCodeAt(i);
            h = h >>> 0;
        }

        // Produce a 64-char hex string by mixing h with position-dependent values
        var hex = '';
        for (var round = 0; round < 8; round++) {
            var v = (h ^ (seed * (round + 1) * 0x9e3779b9)) >>> 0;
            v = ((v >>> 16) ^ v) >>> 0;
            v = ((v * 0x45d9f3b) >>> 0);
            hex += ('00000000' + v.toString(16)).slice(-8);
            h   = (h ^ v) >>> 0;
        }
        return hex;
    }

    function isoTimestamp() {
        return new Date().toISOString().replace(/[-:]/g, '').replace(/\..+/, 'Z');
    }

    function sign(method, uri) {
        var ts        = isoTimestamp();
        var canonical = method + '\n' + uri + '\n' + ts;
        var sig       = pseudoHmac(SECRET_KEY, canonical);
        return {
            timestamp: ts,
            signature: sig,
            authorization: 'Hmac-SHA256 Credential=' + KEY_ID +
                           ',Timestamp=' + ts +
                           ',Signature=' + sig
        };
    }

    // -------------------------------------------------------------------------
    // Route
    // -------------------------------------------------------------------------
    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/sign/'; });
    });

    function parseArgs(qs) {
        var out = {};
        (qs || '').split('&').forEach(function (part) {
            var kv = part.split('=');
            if (kv[0]) out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || '');
        });
        return out;
    }

    server.locations.find(function (l) { return l.path === '/sign/'; })
        .handler = function (r) {
            var path   = parseArgs(r.args)['path'] || r.uri;
            var method = r.method;

            var result = sign(method, path);

            // In a real proxy scenario this handler would:
            //   1. Call r.subrequest(upstreamPath, {headers: {Authorization: result.authorization}})
            // Instead we echo the would-be headers so the demo is self-contained.
            r.respond(200,
                {
                    'Content-Type':  'application/json',
                    'X-Signature':   result.signature,
                    'X-Timestamp':   result.timestamp,
                    'X-Auth-Header': result.authorization
                },
                JSON.stringify({
                    note:          'these headers would be added to the upstream request',
                    method:        method,
                    path:          path,
                    timestamp:     result.timestamp,
                    authorization: result.authorization
                }, null, 2) + '\n');
        };
})();
