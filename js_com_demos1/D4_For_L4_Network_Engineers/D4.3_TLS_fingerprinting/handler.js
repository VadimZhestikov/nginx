// D4.3 — TLS Fingerprinting (HTTP simulation)
//
// PRODUCTION CONCEPT:
//   In a real L4/TLS deployment, the nginx stream{} module reads the raw
//   TLS ClientHello bytes and computes a JA3 fingerprint:
//     JA3 = MD5(SSLVersion,Ciphers,Extensions,EllipticCurves,EllipticCurvePointFormats)
//
//   Different clients produce distinct JA3 hashes — browsers, curl, Python
//   requests, and bots each have recognizable fingerprints that can be used
//   to route, rate-limit, or challenge suspicious clients before the TLS
//   handshake completes.
//
// THIS DEMO:
//   Simulates the same routing logic using HTTP headers:
//     X-TLS-Fingerprint — simulates a precomputed JA3 hash
//     User-Agent        — secondary heuristic for client classification
//
//   Classification rules (in priority order):
//     1. X-TLS-Fingerprint header present and in known-bot list → bot path
//     2. User-Agent contains "bot", "crawler", "spider", "curl" → bot path
//     3. User-Agent matches known API client patterns → api path
//     4. Default → browser path
//
// SharedArrayBuffer accumulates routing stats per client class.

// Known bot JA3 fingerprints (demo values)
var BOT_FINGERPRINTS = {
    'a0e9f5d64349fb13191bc781f81f42e1': 'Googlebot',
    '6bea35d2bb879da64b7da7359e7a6c96': 'Screaming Frog',
    'c8b4d96b72aba5ff68db7e88e36c87a2': 'Semrush bot'
};

// SAB: [browser_count, bot_count, api_count, unknown_count]
var statsSab = new SharedArrayBuffer(16);
var statsArr = new Int32Array(statsSab);

(function () {
    var server = nginx.http.servers[0];

    // ── Internal backends ────────────────────────────────────────────

    server.findLocation('/internal/bot-backend/').handler = function (r) {
        r.respond(200, {
            'X-Client-Class': 'bot',
            'X-Robots-Tag': 'noindex'
        }, 'bot detected — serving limited content (no JS, no sensitive data)\n');
    };

    server.findLocation('/internal/browser-backend/').handler = function (r) {
        r.respond(200, {
            'X-Client-Class': 'browser',
            'Content-Type': 'text/html'
        }, '<html><body><h1>Welcome, human!</h1><p>Full site loaded.</p></body></html>\n');
    };

    server.findLocation('/internal/api-backend/').handler = function (r) {
        r.respond(200, {
            'X-Client-Class': 'api',
            'Content-Type': 'application/json'
        }, JSON.stringify({ status: 'ok', client: 'api', version: '1.0' }) + '\n');
    };

    // ── Client classifier ────────────────────────────────────────────

    function classifyClient(r) {
        var fp = (r.headers['x-tls-fingerprint'] || '').toLowerCase().trim();
        var ua = (r.headers['user-agent'] || '').toLowerCase();

        // Priority 1: known bot fingerprint
        if (fp && BOT_FINGERPRINTS[fp]) {
            return { class: 'bot', reason: 'known_fingerprint:' + BOT_FINGERPRINTS[fp] };
        }

        // Priority 2: bot User-Agent patterns
        var botPatterns = ['bot', 'crawler', 'spider', 'curl', 'wget', 'python-requests', 'scrapy'];
        for (var i = 0; i < botPatterns.length; i++) {
            if (ua.indexOf(botPatterns[i]) !== -1) {
                return { class: 'bot', reason: 'ua_pattern:' + botPatterns[i] };
            }
        }

        // Priority 3: API client patterns
        var apiPatterns = ['okhttp', 'axios', 'node-fetch', 'go-http-client', 'java/', 'ruby'];
        for (var i = 0; i < apiPatterns.length; i++) {
            if (ua.indexOf(apiPatterns[i]) !== -1) {
                return { class: 'api', reason: 'api_client:' + apiPatterns[i] };
            }
        }

        // Default: browser
        return { class: 'browser', reason: 'default' };
    }

    // ── /detect/ — fingerprint, classify, route ──────────────────────
    server.findLocation('/detect/').handler = async function (r) {
        var classification = classifyClient(r);
        var cls = classification.class;

        var slot = cls === 'browser' ? 0 : cls === 'bot' ? 1 : cls === 'api' ? 2 : 3;
        Atomics.add(statsArr, slot, 1);

        var target = '/internal/' + cls + '-backend/';
        nginx.log(4, 'TLS fingerprint routing: class=' + cls +
            ' reason=' + classification.reason);

        var res = await r.subrequest(target);
        r.respond(res.status, {
            'X-Client-Class': cls,
            'X-Fingerprint-Reason': classification.reason,
            'Content-Type': res.headers['Content-Type'] || 'text/plain'
        }, res.body);
    };

    // ── /status/ — routing statistics ───────────────────────────────
    server.findLocation('/status/').handler = function (r) {
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            routing_stats: {
                browser: Atomics.load(statsArr, 0),
                bot:     Atomics.load(statsArr, 1),
                api:     Atomics.load(statsArr, 2),
                unknown: Atomics.load(statsArr, 3)
            },
            known_bot_fingerprints: Object.keys(BOT_FINGERPRINTS).length,
            note: 'Production: JA3 fingerprint from TLS ClientHello bytes'
        }, null, 2) + '\n');
    };

})();
