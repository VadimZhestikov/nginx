/**
 * Zero-Trust API Gateway — JS-Pilgrim reference application.
 *
 * Demonstrates complete wrapping of nginx's phase engine using all
 * JS-Pilgrim layers (P1–P17):
 *
 *   L4 inbound filter   — strip a proprietary NGXGW: framing prefix
 *   Accept hook         — IP allowlist + per-IP rate limit (tcp level)
 *   Global HTTP hook    — JWT authentication, populate req.ctx
 *   Location hook       — RBAC check + request counter (with next())
 *   Upstream req filter — inject audit metadata into forwarded body
 *   Upstream res filter — scrub _internal_* fields from backend JSON
 *   Response hook       — add security headers, set x-request-id
 *   Body filter         — inject <!-- reqId:... --> into HTML responses
 *   L4 send filter      — passthrough (demonstrates outbound layer)
 *   Master events       — log worker lifecycle to nginx.shared
 *   /status handler     — expose cross-worker counters as JSON
 *
 * Usage (nginx.conf):
 *   js_source /path/to/gateway.js;
 *
 * The gateway assumes server_name is 'gateway' (or falls back to
 * nginx.http.servers[0] if no server matches).
 */

(function () {
    'use strict';

    /* ------------------------------------------------------------------
     * Configuration
     * ------------------------------------------------------------------ */

    var CFG = {
        /* Framing prefix that trusted gateways prepend to raw TCP data.
         * Strip it before the HTTP parser sees the bytes.            */
        l4InboundPrefix: 'NGXGW:',

        /* JWT: minimum required fields.  Signature is NOT verified here
         * (demo only).  Replace parseJWT() with a real HMAC check in
         * production.                                                 */
        jwtRequired: true,

        /* RBAC: paths under this prefix require the 'admin' role.     */
        adminPrefix: '/api/admin/',

        /* Rate limit: max accepted connections per IP per 60-second
         * window.  Uses nginx.shared for cross-worker atomicity.      */
        rateLimitMax:    60,
        rateLimitWindow: 60,   /* seconds */

        /* Paths that bypass JWT authentication.                       */
        publicPaths: ['/healthz', '/status'],
    };

    /* ------------------------------------------------------------------
     * Utility helpers
     * ------------------------------------------------------------------ */

    function findServer(name) {
        var servers = nginx.http.servers;
        for (var i = 0; i < servers.length; i++) {
            if (servers[i].name === name) { return servers[i]; }
        }
        return servers[0];   /* fallback: single-server configs */
    }

    function findLoc(srv, path) {
        var locs = srv.locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === path) { return locs[i]; }
        }
        return null;
    }

    /* base64url decode (nginx JS runtime has no atob built-in) */
    var B64C = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    function b64urlDecode(s) {
        s = s.replace(/-/g, '+').replace(/_/g, '/').replace(/=/g, '');
        var out = '', i = 0;
        while (i < s.length) {
            var b0 = B64C.indexOf(s[i++]);
            var b1 = i < s.length ? B64C.indexOf(s[i++]) : -1;
            var b2 = i < s.length ? B64C.indexOf(s[i++]) : -1;
            var b3 = i < s.length ? B64C.indexOf(s[i++]) : -1;
            if (b0 < 0 || b1 < 0) { break; }
            out += String.fromCharCode((b0 << 2) | (b1 >> 4));
            if (b2 >= 0) { out += String.fromCharCode(((b1 & 0xf) << 4) | (b2 >> 2)); }
            if (b3 >= 0) { out += String.fromCharCode(((b2 & 0x3) << 6) | b3); }
        }
        return out;
    }

    /* Parse a Bearer JWT.  Returns claims object on success, null on
     * failure or expiry.  Signature is NOT verified (demo).           */
    function parseJWT(authHeader) {
        if (!authHeader || !authHeader.startsWith('Bearer ')) { return null; }
        try {
            var parts = authHeader.slice(7).split('.');
            if (parts.length < 2) { return null; }
            var claims = JSON.parse(b64urlDecode(parts[1]));
            if (claims.exp && claims.exp < Date.now() / 1000) { return null; }
            return claims;
        } catch (e) {
            nginx.log(nginx.WARN, 'gateway: JWT parse error: ' + e);
            return null;
        }
    }

    function isPublicPath(uri) {
        for (var i = 0; i < CFG.publicPaths.length; i++) {
            if (uri === CFG.publicPaths[i] ||
                uri.startsWith(CFG.publicPaths[i] + '/')) {
                return true;
            }
        }
        return false;
    }

    /* ------------------------------------------------------------------
     * Locate the gateway server and its API location
     * ------------------------------------------------------------------ */

    var srv    = findServer('gateway');
    var apiLoc = findLoc(srv, '/api/');

    if (!srv) {
        nginx.log(nginx.ERR, 'gateway: server "gateway" not found');
        return;
    }

    /* ==================================================================
     * Layer 1 — L4 inbound filter
     * Strip the optional NGXGW: framing prefix before HTTP parsing.
     * ================================================================== */

    srv.addL4Filter(async function* (source) {
        for await (const chunk of source) {
            var s = String.fromCharCode.apply(null, Array.from(chunk));
            if (s.startsWith(CFG.l4InboundPrefix)) {
                nginx.log(nginx.DEBUG,
                          'l4: stripped ' + CFG.l4InboundPrefix + ' prefix');
                yield s.slice(CFG.l4InboundPrefix.length);
            } else {
                yield s;
            }
            return;   /* one chunk covers the HTTP request line */
        }
    });

    /* ==================================================================
     * Layer 2 — Accept hook
     * IP allowlist check and per-IP sliding-window rate limit.
     * Both use nginx.shared for visibility across workers.
     * ================================================================== */

    /* IP allowlist — empty = allow all.  Populate for production.     */
    var ALLOWED_PREFIXES = [
        /* '10.',          */
        /* '192.168.',     */
        /* '127.',         */   /* enable for local-only access */
    ];

    function ipAllowed(ip) {
        if (ALLOWED_PREFIXES.length === 0) { return true; }   /* open */
        for (var i = 0; i < ALLOWED_PREFIXES.length; i++) {
            if (ip.startsWith(ALLOWED_PREFIXES[i])) { return true; }
        }
        return false;
    }

    srv.on('accept', function (conn) {
        var ip = conn.remoteAddr;

        if (!ipAllowed(ip)) {
            nginx.log(nginx.WARN, 'gateway: blocked IP ' + ip);
            conn.reject();
            return;
        }

        /* Sliding-window rate limit (per worker — approximation).
         * For cross-worker accuracy, pair with an nginx limit_req zone.  */
        var key    = 'rl:' + ip;
        var tsKey  = 'rl:' + ip + ':ts';
        var now    = Math.floor(Date.now() / 1000);
        var winTs  = parseInt(nginx.shared.get(tsKey) || '0');

        if (now - winTs >= CFG.rateLimitWindow) {
            /* New or expired window — reset.                          */
            nginx.shared.set(key,   '1');
            nginx.shared.set(tsKey, String(now));
        } else {
            var hits = nginx.shared.incr(key);
            if (hits > CFG.rateLimitMax) {
                nginx.log(nginx.WARN,
                          'gateway: rate-limit exceeded for ' + ip +
                          ' (' + hits + ' connections this window)');
                conn.reject();
                return;
            }
        }

        nginx.log(nginx.DEBUG, 'gateway: accepted ' + ip);
    });

    /* ==================================================================
     * Layer 3 — Global HTTP hook (access phase)
     * JWT authentication.  Stores decoded claims in req.ctx for all
     * downstream layers to read.
     * ================================================================== */

    nginx.http.addHook(async function (req) {
        if (isPublicPath(req.uri)) { return; }

        var claims = parseJWT(req.headers['authorization']);
        if (!claims) {
            req.respond(401,
                { 'Content-Type': 'application/json',
                  'WWW-Authenticate': 'Bearer realm="api"' },
                '{"error":"unauthorized"}\n');
            return;
        }

        req.ctx.sub   = claims.sub   || 'anonymous';
        req.ctx.roles = claims.roles || [];
        req.ctx.reqId = claims.jti   || (Math.random().toString(36).slice(2));
        req.ctx.t0    = Date.now();

        nginx.log(nginx.INFO,
                  'gateway: auth ok sub=' + req.ctx.sub +
                  ' jti=' + req.ctx.reqId);
    });

    /* ==================================================================
     * Layer 4 — Location hook (content phase) with next()
     * RBAC enforcement + request counting.
     *
     * Code before next() runs before the content handler (proxy_pass).
     * Code after next() also runs before the content handler — nginx
     * runs the content handler only once the entire chain has settled.
     * Elapsed time including proxy round-trip is measured in Layer 7
     * (addResponseHook), which fires after proxy_pass completes.
     * ================================================================== */

    if (apiLoc) {
        apiLoc.addHook(async function (req, next) {
            /* RBAC: /api/admin/* requires the 'admin' role.           */
            if (req.uri.startsWith(CFG.adminPrefix)) {
                var roles = (req.ctx && req.ctx.roles) || [];
                var isAdmin = false;
                for (var i = 0; i < roles.length; i++) {
                    if (roles[i] === 'admin') { isAdmin = true; break; }
                }
                if (!isAdmin) {
                    req.respond(403,
                        { 'Content-Type': 'application/json' },
                        '{"error":"forbidden"}\n');
                    return;
                }
            }

            await next();

            /* Count authorized API requests in shared memory.        */
            nginx.shared.incr('total_requests');

            nginx.log(nginx.INFO,
                      'gateway: ' + req.method + ' ' + req.uri +
                      ' sub=' + ((req.ctx && req.ctx.sub) || '-'));
        });
    }

    /* ==================================================================
     * Layer 5 — Upstream request filter (P14)
     * Inject audit metadata into the outbound JSON request body.
     * ================================================================== */

    if (apiLoc) {
        apiLoc.addUpstreamRequestFilter(async function* (body, req) {
            try {
                var obj = JSON.parse(body);
                obj._gateway = {
                    sub:  (req.ctx && req.ctx.sub)   || 'unknown',
                    jti:  (req.ctx && req.ctx.reqId)  || '',
                    ts:   new Date().toISOString(),
                };
                yield JSON.stringify(obj);
            } catch (e) {
                yield body;   /* not JSON — forward unchanged */
            }
        });
    }

    /* ==================================================================
     * Layer 6 — Upstream response filter (P14)
     * Scrub _internal_* fields from backend JSON responses before
     * the client sees them.
     * ================================================================== */

    if (apiLoc) {
        apiLoc.addUpstreamFilter(async function* (body, req) {
            try {
                var obj = JSON.parse(body);
                var keys = Object.keys(obj);
                for (var i = 0; i < keys.length; i++) {
                    if (keys[i].indexOf('_internal') === 0) {
                        delete obj[keys[i]];
                    }
                }
                yield JSON.stringify(obj);
            } catch (e) {
                yield body;   /* not JSON — forward unchanged */
            }
        });
    }

    /* ==================================================================
     * Layer 7 — Response hook (P3)
     * Add security headers after the content handler (proxy_pass) has
     * selected the upstream response headers.  This fires AFTER the
     * proxy round-trip, so timing here reflects the full backend latency.
     * ================================================================== */

    if (apiLoc) {
        apiLoc.addResponseHook(function (req) {
            /* Remove identifying headers.                             */
            req.removeHeader('server');
            req.removeHeader('x-powered-by');

            /* OWASP-recommended security headers.                     */
            req.setHeader('x-content-type-options', 'nosniff');
            req.setHeader('x-frame-options',        'DENY');
            req.setHeader('cache-control',
                                  'no-store, no-cache, must-revalidate');

            /* Propagate request identity from JWT to response.        */
            if (req.ctx && req.ctx.reqId) {
                req.setHeader('x-request-id', req.ctx.reqId);
            }

            /* Response latency (start time set by the access hook).   */
            if (req.ctx && req.ctx.t0) {
                req.setHeader('x-response-ms',
                                      String(Date.now() - req.ctx.t0));
            }
        });
    }

    /* ==================================================================
     * Layer 8 — Body filter (P5)
     * Inject a signed request-ID HTML comment into HTML responses so
     * every page carry a traceable identifier.
     * ================================================================== */

    if (apiLoc) {
        apiLoc.addBodyFilter('wholeBodySync', function (req, body) {
            var ct = req.getHeader('content-type') || '';
            if (ct.indexOf('text/html') === -1) { return undefined; }
            var reqId = (req.ctx && req.ctx.reqId) || 'unknown';
            var tag   = '<!-- reqId:' + reqId + ' -->';
            if (body.indexOf('</body>') !== -1) {
                return body.replace('</body>', tag + '</body>');
            }
            return body + tag;
        });
    }

    /* ==================================================================
     * Layer 9 — L4 send filter (P13/P17)
     * Passthrough — demonstrates the outbound layer is in place.
     * Production use-cases: custom protocol framing, traffic shaping.
     * ================================================================== */

    srv.addL4SendFilter(async function* (source) {
        for await (const chunk of source) {
            yield chunk;
        }
    });

    /* ==================================================================
     * Master lifecycle events
     * Count worker spawns/exits in nginx.shared for the /status page.
     * These callbacks run in the master process.
     * ================================================================== */

    nginx.on('workerSpawned', function (pid, slot) {
        nginx.shared.incr('workers_started');
        nginx.log(nginx.NOTICE,
                  'gateway: worker pid=' + pid + ' slot=' + slot + ' started');
    });

    nginx.on('workerExited', function (pid, slot, status) {
        nginx.shared.incr('workers_exited');
        nginx.log(nginx.NOTICE,
                  'gateway: worker pid=' + pid + ' exited status=' + status);
    });

    nginx.on('reload', function () {
        nginx.log(nginx.NOTICE, 'gateway: configuration reload requested');
    });

    /* ==================================================================
     * /status endpoint
     * Pure-JS handler — no upstream involved.  Reports cross-worker
     * counters from nginx.shared.
     * ================================================================== */

    var statusLoc = findLoc(srv, '/status');
    if (statusLoc) {
        statusLoc.handler = function (req) {
            var info = {
                total_requests:  parseInt(nginx.shared.get('total_requests')  || '0'),
                workers_started: parseInt(nginx.shared.get('workers_started') || '0'),
                workers_exited:  parseInt(nginx.shared.get('workers_exited')  || '0'),
            };
            req.respond(200,
                { 'Content-Type': 'application/json' },
                JSON.stringify(info, null, 2) + '\n');
        };
    }

    /* ==================================================================
     * /healthz endpoint
     * Ultra-lightweight — no JWT, no proxy.  Returns 200 immediately.
     * ================================================================== */

    var healthzLoc = findLoc(srv, '/healthz');
    if (healthzLoc) {
        healthzLoc.handler = function (req) {
            req.respond(200, { 'Content-Type': 'text/plain' }, 'ok\n');
        };
    }

    nginx.log(nginx.NOTICE, 'gateway: initialised');

})();
