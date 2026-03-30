#!/usr/bin/perl

# Tests for the Zero-Trust API Gateway reference application.
#
# Covers all nine JS-Pilgrim layers in one test suite:
#
#   L4 inbound filter   — NGXGW: prefix stripped before HTTP parsing
#   Accept hook         — per-IP connection counting (rate-limit plumbing)
#   Global HTTP hook    — JWT auth: 401 on missing/expired token
#   Location hook       — RBAC: 403 for user on /api/admin/*
#   Upstream req filter — _gateway audit fields injected into request body
#   Upstream res filter — _internal_* fields scrubbed from response
#   Response hook       — security headers set; Server: removed
#   Body filter         — <!-- reqId:... --> injected into HTML
#   L4 send filter      — passthrough: does not corrupt HTTP responses
#
# Tests:
#   1-2   /healthz: no auth required, 200 + correct body
#   3-4   No Authorization header -> 401, body has "unauthorized"
#   5     Expired JWT -> 401
#   6-8   Valid user JWT, /api/data/ -> 200, has "result", no "_internal_trace"
#   9-11  Security headers: x-content-type-options, x-frame-options, no Server:
#   12-13 x-request-id present and matches jti from token
#   14-15 User JWT, /api/admin/items/ -> 403, body has "forbidden"
#   16-18 Admin JWT, /api/admin/items/ -> 200, has "admin", no "_internal_id"
#   19    HTML response has <!-- reqId: --> comment injected
#   20    NGXGW: prefix stripped: raw socket request succeeds
#   21-23 Upstream req filter: POST with JSON body -> echoed body has _gateway
#   24-25 /status: 200, body has total_requests

use warnings;
use strict;
use Test::More;
use MIME::Base64 qw(encode_base64);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(25);

# -----------------------------------------------------------------------
# JWT token helpers
# -----------------------------------------------------------------------

sub b64url {
    (my $s = encode_base64($_[0], '')) =~ tr|+/=|-_|d;
    return $s;
}

sub make_token {
    my ($sub, $roles_ref, $exp, $jti) = @_;
    my $roles = '["' . join('","', @$roles_ref) . '"]';
    my $hdr   = b64url('{"alg":"none"}');
    my $pld   = b64url(
        sprintf('{"sub":"%s","roles":%s,"exp":%d,"jti":"%s"}',
                $sub, $roles, $exp, $jti));
    return "Bearer $hdr.$pld.sig";
}

my $USER_TOKEN  = make_token('user1',  ['user'],          9_999_999_999, 'tid-u1');
my $ADMIN_TOKEN = make_token('admin1', ['admin', 'user'], 9_999_999_999, 'tid-a1');
my $EXPIRED_TOK = make_token('user2',  ['user'],          1,             'tid-e1');

# -----------------------------------------------------------------------
# nginx.conf: backend (servers[0]) + gateway (servers[1])
# -----------------------------------------------------------------------

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    # Backend server (simulates upstream API service)
    server {
        listen      127.0.0.1:%%PORT_8082%%;
        server_name backend;

        location /data/ {
            default_type application/json;
            return 200 '{"result":"ok","_internal_trace":"secret-abc"}';
        }

        location /admin/items/ {
            default_type application/json;
            return 200 '{"admin":true,"items":["x","y"],"_internal_id":"adm-secret"}';
        }

        location /html/ {
            default_type text/html;
            return 200 '<html><body>Hello world</body></html>';
        }

        location /echo/ { }
    }

    # Gateway server — all hooks wired by init.js
    server {
        listen      127.0.0.1:8080;
        server_name gateway;

        location /healthz  { }
        location /status   { }
        location /api/ {
            proxy_pass http://127.0.0.1:%%PORT_8082%%/;
        }
    }
}
EOF

# -----------------------------------------------------------------------
# init.js: backend /echo/ handler + full gateway pipeline
# -----------------------------------------------------------------------

$t->write_file('init.js', <<'JS');
(function () {
    'use strict';

    /* ------------------------------------------------------------------
     * Backend server (servers[0]): wire /echo/ to a request-body echo
     * ------------------------------------------------------------------ */
    (function () {
        var locs = nginx.http.servers[0].locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === '/echo/') {
                locs[i].handler = async function (req) {
                    var body = await req.readBody();
                    req.respond(200,
                        { 'Content-Type': 'application/json' },
                        body || '{}');
                };
            }
        }
    }());

    /* ------------------------------------------------------------------
     * Gateway server (servers[1]): full pipeline
     * ------------------------------------------------------------------ */
    (function () {

        /* helpers */
        function findLoc(srv, path) {
            var locs = srv.locations;
            for (var i = 0; i < locs.length; i++) {
                if (locs[i].path === path) { return locs[i]; }
            }
            return null;
        }

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

        function parseJWT(authHeader) {
            if (!authHeader || !authHeader.startsWith('Bearer ')) { return null; }
            try {
                var parts  = authHeader.slice(7).split('.');
                var claims = JSON.parse(b64urlDecode(parts[1]));
                if (claims.exp && claims.exp < Date.now() / 1000) { return null; }
                return claims;
            } catch (e) { return null; }
        }

        var srv    = nginx.http.servers[1];
        var apiLoc = findLoc(srv, '/api/');

        /* Layer 1: L4 inbound filter — strip NGXGW: prefix */
        srv.addL4Filter(async function* (source) {
            for await (const chunk of source) {
                var s = String.fromCharCode.apply(null, Array.from(chunk));
                yield s.startsWith('NGXGW:') ? s.slice(6) : s;
                return;
            }
        });

        /* Layer 2: Accept hook — count connections */
        srv.on('accept', function (conn) {
            nginx.shared.incr('accept_count');
        });

        /* Layer 3: Global HTTP hook — JWT auth */
        nginx.http.addHook(async function (req) {
            if (req.uri === '/healthz' || req.uri === '/status') { return; }
            var claims = parseJWT(req.headers['authorization']);
            if (!claims) {
                req.respond(401,
                    { 'Content-Type': 'application/json',
                      'WWW-Authenticate': 'Bearer realm="api"' },
                    '{"error":"unauthorized"}\n');
                return;
            }
            req.ctx.sub   = claims.sub   || 'anon';
            req.ctx.roles = claims.roles || [];
            req.ctx.reqId = claims.jti   || '';
            req.ctx.t0    = Date.now();
        });

        /* Layer 4: Location hook — RBAC + request counting */
        if (apiLoc) {
            apiLoc.addHook(async function (req, next) {
                if (req.uri.startsWith('/api/admin/')) {
                    var roles  = (req.ctx && req.ctx.roles) || [];
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
                nginx.shared.incr('total_requests');
            });
        }

        /* Layer 5: Upstream request filter — inject _gateway audit fields */
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
                    yield body;
                }
            });
        }

        /* Layer 6: Upstream response filter — scrub _internal_* fields */
        if (apiLoc) {
            apiLoc.addUpstreamFilter(async function* (body, req) {
                try {
                    var obj  = JSON.parse(body);
                    var keys = Object.keys(obj);
                    for (var i = 0; i < keys.length; i++) {
                        if (keys[i].indexOf('_internal') === 0) {
                            delete obj[keys[i]];
                        }
                    }
                    yield JSON.stringify(obj);
                } catch (e) {
                    yield body;
                }
            });
        }

        /* Layer 7: Response hook — security headers */
        if (apiLoc) {
            apiLoc.addResponseHook(function (req) {
                req.removeHeader('server');
                req.removeHeader('x-powered-by');
                req.setHeader('x-content-type-options', 'nosniff');
                req.setHeader('x-frame-options',        'DENY');
                req.setHeader('cache-control',
                                      'no-store, no-cache, must-revalidate');
                if (req.ctx && req.ctx.reqId) {
                    req.setHeader('x-request-id', req.ctx.reqId);
                }
            });
        }

        /* Layer 8: Body filter — inject reqId comment into HTML */
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

        /* Layer 9: L4 send filter — passthrough */
        srv.addL4SendFilter(async function* (source) {
            for await (const chunk of source) {
                yield chunk;
            }
        });

        /* /status handler */
        var statusLoc = findLoc(srv, '/status');
        if (statusLoc) {
            statusLoc.handler = function (req) {
                var info = {
                    total_requests:  parseInt(nginx.shared.get('total_requests')  || '0'),
                    workers_started: parseInt(nginx.shared.get('workers_started') || '0'),
                    accept_count:    parseInt(nginx.shared.get('accept_count')    || '0'),
                };
                req.respond(200,
                    { 'Content-Type': 'application/json' },
                    JSON.stringify(info) + '\n');
            };
        }

        /* /healthz handler */
        var healthzLoc = findLoc(srv, '/healthz');
        if (healthzLoc) {
            healthzLoc.handler = function (req) {
                req.respond(200, { 'Content-Type': 'text/plain' }, 'ok\n');
            };
        }

    }());
}());
JS

$t->run();

# -----------------------------------------------------------------------
# 1-2: /healthz — no auth required
# -----------------------------------------------------------------------

my $r = http_get('/healthz');
like($r, qr{200 OK},  'healthz: 200 OK');
like($r, qr{\bok\b}m, 'healthz: body is ok');

# -----------------------------------------------------------------------
# 3-4: No Authorization header → 401
# -----------------------------------------------------------------------

$r = http_get('/api/data/');
like($r, qr{401},                    'no-auth: 401');
like($r, qr{"error":"unauthorized"}, 'no-auth: error body');

# -----------------------------------------------------------------------
# 5: Expired JWT → 401
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $EXPIRED_TOK

EOF
like($r, qr{401}, 'expired-token: 401');

# -----------------------------------------------------------------------
# 6-8: Valid user JWT, /api/data/ — 200, result present, _internal scrubbed
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $USER_TOKEN

EOF
like($r, qr{200 OK},            'user/data: 200 OK');
like($r, qr{"result"},          'user/data: result field present');
unlike($r, qr{_internal_trace}, 'user/data: _internal_trace scrubbed');

# -----------------------------------------------------------------------
# 9-11: Security headers
# -----------------------------------------------------------------------

like($r,   qr{x-content-type-options:\s*nosniff}i,
                                'sec-hdr: x-content-type-options nosniff');
like($r,   qr{x-frame-options:\s*DENY}i,
                                'sec-hdr: x-frame-options DENY');
unlike($r, qr{^Server:}im,      'sec-hdr: Server header removed');

# -----------------------------------------------------------------------
# 12-13: x-request-id present and matches jti from user token
# -----------------------------------------------------------------------

like($r,   qr{x-request-id:}i,  'reqid-hdr: x-request-id present');
like($r,   qr{x-request-id:\s*tid-u1}i,
                                 'reqid-hdr: value matches jti tid-u1');

# -----------------------------------------------------------------------
# 14-15: User JWT, /api/admin/items/ → 403 RBAC
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /api/admin/items/ HTTP/1.0
Host: localhost
Authorization: $USER_TOKEN

EOF
like($r,   qr{403},                  'rbac: user on admin path → 403');
like($r,   qr{"error":"forbidden"},  'rbac: forbidden body');

# -----------------------------------------------------------------------
# 16-18: Admin JWT, /api/admin/items/ → 200, _internal_id scrubbed
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /api/admin/items/ HTTP/1.0
Host: localhost
Authorization: $ADMIN_TOKEN

EOF
like($r,   qr{200 OK},         'admin: 200 OK');
like($r,   qr{"admin":true},   'admin: admin field present');
unlike($r, qr{_internal_id},   'admin: _internal_id scrubbed');

# -----------------------------------------------------------------------
# 19: HTML response — <!-- reqId:... --> injected
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /api/html/ HTTP/1.0
Host: localhost
Authorization: $USER_TOKEN

EOF
like($r, qr{<!-- reqId:tid-u1 -->}, 'html-filter: reqId comment injected');

# -----------------------------------------------------------------------
# 20: L4 inbound filter — NGXGW: prefix stripped
# Send a raw request with the NGXGW: prefix; nginx must see valid HTTP.
# -----------------------------------------------------------------------

$r = http('NGXGW:' . <<EOF);
GET /healthz HTTP/1.0
Host: localhost

EOF
like($r, qr{200 OK}, 'l4-filter: NGXGW: prefix stripped, 200 OK');

# -----------------------------------------------------------------------
# 21-23: Upstream request filter — _gateway fields injected
# POST JSON to /api/echo/ (backend echoes the body); the upstream req
# filter enriches the body before the backend receives it.
# -----------------------------------------------------------------------

my $body = '{"key":"value"}';
$r = http(<<"EOF");
POST /api/echo/ HTTP/1.0
Host: localhost
Authorization: $USER_TOKEN
Content-Type: application/json
Content-Length: ${\ length($body) }

$body
EOF

like($r,   qr{200 OK},       'upstream-req-filter: 200 OK');
like($r,   qr{"_gateway"},   'upstream-req-filter: _gateway injected');
like($r,   qr{"sub":"user1"},'upstream-req-filter: sub field correct');

# -----------------------------------------------------------------------
# 24-25: /status — cross-worker counters
# -----------------------------------------------------------------------

$r = http(<<EOF);
GET /status HTTP/1.0
Host: localhost

EOF
like($r, qr{200 OK},             'status: 200 OK');
like($r, qr{"total_requests":},  'status: total_requests field present');
