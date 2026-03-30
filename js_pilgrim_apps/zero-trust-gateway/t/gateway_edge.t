#!/usr/bin/perl

# Edge-case tests for the Zero-Trust API Gateway.
#
# Covers scenarios NOT in gateway.t:
#   1-2   JWT without 'exp' field — allowed (no-expiry token)
#   3-4   Malformed JWT (fewer than 3 dot-separated parts) — 401
#   5     WWW-Authenticate header present on 401
#   6     cache-control: no-store on authenticated API response
#   7-8   HTML without </body> tag — reqId appended to end of body
#   9     Multiple _internal_* fields all scrubbed in one pass
#  10-11  /healthz bypasses JWT auth even when Authorization header is present
#  12-13  /status bypasses JWT auth even when Authorization header is present
#  14     Admin JWT on /api/data/ (non-admin path) — 200 (admin may access)

use warnings;
use strict;
use Test::More;
use MIME::Base64 qw(encode_base64);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(16);

# -----------------------------------------------------------------------
# JWT helpers
# -----------------------------------------------------------------------

sub b64url {
    (my $s = encode_base64($_[0], '')) =~ tr|+/=|-_|d;
    return $s;
}

sub make_token {
    my ($claims_href) = @_;
    my $hdr = b64url('{"alg":"none"}');
    use JSON::PP;
    my $pld = b64url(JSON::PP->new->encode($claims_href));
    return "Bearer $hdr.$pld.sig";
}

# Token without 'exp' field — should be accepted
my $NO_EXP_TOKEN  = make_token({ sub => 'u-noexp', roles => ['user'],
                                  jti => 'tid-noexp' });

# Token with exp far in the future
my $USER_TOKEN    = make_token({ sub => 'user1', roles => ['user'],
                                  exp => 9_999_999_999, jti => 'tid-u1' });

my $ADMIN_TOKEN   = make_token({ sub => 'admin1', roles => ['admin','user'],
                                  exp => 9_999_999_999, jti => 'tid-a1' });

# Malformed — not enough dot-separated parts
my $MALFORMED_TOK = 'Bearer onlyonepart';

# -----------------------------------------------------------------------
# nginx.conf
# -----------------------------------------------------------------------

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:%%PORT_8082%%;
        server_name backend;

        location /data/ {
            default_type application/json;
            return 200 '{"result":"ok","_internal_a":"x","_internal_b":"y"}';
        }

        location /html-nobody/ {
            default_type text/html;
            return 200 '<html><body>No close tag here';
        }
    }

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

$t->write_file('init.js', <<'JS');
(function () {
    'use strict';

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
            if (parts.length < 2) { return null; }
            var claims = JSON.parse(b64urlDecode(parts[1]));
            /* Only reject if exp is present AND in the past */
            if (claims.exp && claims.exp < Date.now() / 1000) { return null; }
            return claims;
        } catch (e) { return null; }
    }

    function findLoc(srv, path) {
        var locs = srv.locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === path) { return locs[i]; }
        }
        return null;
    }

    var srv    = nginx.http.servers[1];
    var apiLoc = findLoc(srv, '/api/');

    /* JWT auth */
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
    });

    /* Security headers + x-request-id */
    if (apiLoc) {
        apiLoc.addResponseHook(function (req) {
            req.removeHeader('server');
            req.setHeader('x-content-type-options', 'nosniff');
            req.setHeader('x-frame-options', 'DENY');
            req.setHeader('cache-control', 'no-store, no-cache, must-revalidate');
            if (req.ctx && req.ctx.reqId) {
                req.setHeader('x-request-id', req.ctx.reqId);
            }
        });
    }

    /* Scrub _internal_* fields */
    if (apiLoc) {
        apiLoc.addUpstreamFilter(async function*(chunks, req) {
            for await (var chunk of chunks) {
                try {
                    var obj  = JSON.parse(chunk);
                    var keys = Object.keys(obj);
                    for (var i = 0; i < keys.length; i++) {
                        if (keys[i].indexOf('_internal') === 0) {
                            delete obj[keys[i]];
                        }
                    }
                    yield JSON.stringify(obj);
                } catch (e) { yield chunk; }
            }
        });
    }

    /* HTML body filter */
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

    /* /healthz handler */
    var healthzLoc = findLoc(srv, '/healthz');
    if (healthzLoc) {
        healthzLoc.handler = function (req) {
            req.respond(200, { 'Content-Type': 'text/plain' }, 'ok\n');
        };
    }

    /* /status handler */
    var statusLoc = findLoc(srv, '/status');
    if (statusLoc) {
        statusLoc.handler = function (req) {
            req.respond(200, { 'Content-Type': 'application/json' },
                        '{"status":"ok"}\n');
        };
    }

}());
JS

$t->run();

# -----------------------------------------------------------------------
# 1-2: JWT without 'exp' field — should be accepted (no expiry constraint)
# -----------------------------------------------------------------------

my $r = http(<<"EOF");
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $NO_EXP_TOKEN

EOF
like($r, qr{200 OK},    'no-exp-jwt: 200 OK — token without exp accepted');
like($r, qr{"result"},  'no-exp-jwt: upstream result present');

# -----------------------------------------------------------------------
# 3-4: Malformed JWT (single token, no dots) — 401
# -----------------------------------------------------------------------

$r = http(<<"EOF");
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $MALFORMED_TOK

EOF
like($r, qr{401},                    'malformed-jwt: 401');
like($r, qr{"error":"unauthorized"}, 'malformed-jwt: error body');

# -----------------------------------------------------------------------
# 5: WWW-Authenticate header present on 401
# -----------------------------------------------------------------------

$r = http(<<"GET");
GET /api/data/ HTTP/1.0
Host: localhost

GET
like($r, qr{WWW-Authenticate:}i, '401-www-auth: WWW-Authenticate header present');

# -----------------------------------------------------------------------
# 6: cache-control: no-store set on authenticated API response
# -----------------------------------------------------------------------

$r = http(<<"EOF");
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $USER_TOKEN

EOF
like($r, qr{cache-control:\s*no-store}i, 'cache-control: no-store set on API response');

# -----------------------------------------------------------------------
# 7-8: HTML body without </body> — reqId appended to end of string
# -----------------------------------------------------------------------

$r = http(<<"EOF");
GET /api/html-nobody/ HTTP/1.0
Host: localhost
Authorization: $USER_TOKEN

EOF
like($r,   qr{200 OK},                  'html-nobody: 200 OK');
like($r,   qr{<!-- reqId:tid-u1 -->},   'html-nobody: reqId appended to end (no </body>)');
unlike($r, qr{</body>},                 'html-nobody: no </body> in response');

# -----------------------------------------------------------------------
# 9: Multiple _internal_* fields — all scrubbed in one pass
# -----------------------------------------------------------------------

$r = http(<<"EOF");
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $ADMIN_TOKEN

EOF
unlike($r, qr{_internal_a}, 'multi-internal: _internal_a scrubbed');
unlike($r, qr{_internal_b}, 'multi-internal: _internal_b scrubbed');
like($r,   qr{"result"},    'multi-internal: non-internal result field preserved');

# -----------------------------------------------------------------------
# 10-11: /healthz bypasses JWT auth (no token → still 200)
# -----------------------------------------------------------------------

$r = http("GET /healthz HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{200 OK}, 'healthz-bypass: 200 without any token');
like($r, qr{\bok\b},  'healthz-bypass: body is ok');

# -----------------------------------------------------------------------
# 12-13: /status bypasses JWT auth
# -----------------------------------------------------------------------

$r = http("GET /status HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{200 OK}, 'status-bypass: 200 without any token');

# -----------------------------------------------------------------------
# 14: Admin JWT on non-admin /api/data/ path — 200 (no RBAC restriction)
# -----------------------------------------------------------------------

$r = http(<<"EOF");
GET /api/data/ HTTP/1.0
Host: localhost
Authorization: $ADMIN_TOKEN

EOF
like($r, qr{200 OK}, 'admin-on-data: admin JWT on /api/data/ gives 200');
