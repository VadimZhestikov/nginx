#!/usr/bin/perl

# Tests for the Body JSON Router reference application.
#
# Verifies that:
#   • collectJsonPaths correctly extracts routing fields (including nested)
#   • Routing to each of the four backends works
#   • Early-exit scanning: large bodies with fields near the start still route
#   • Error responses for empty and non-JSON bodies
#   • The original request body reaches the backend via X-Forwarded-Body
#
# Tests:
#   1-2   service=payments  + tenant.region=eu-west  → payments-eu  (200 + body)
#   3-4   service=payments  + tenant.region=us-east  → payments-us  (200 + body)
#   5-6   service=analytics + any region             → analytics    (200 + body)
#   7-8   service=orders    (unknown)                → default      (200 + body)
#   9-10  Empty body                                 → 400 + "error"
#   11-12 Non-JSON body (plain text)                 → 400 + "error"
#   13-14 Large body — routing fields first, 4 KB padding after → 200 + correct route
#   15    Body forwarding — unique marker echoed back from backend

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(15);

# -----------------------------------------------------------------------
# nginx.conf
#   servers[0] — backend (simulates the four upstream pools)
#   servers[1] — router  (JS content handler + internal relay locations)
# -----------------------------------------------------------------------

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    # Simulated backend pools — all on one server for the test.
    # Each location returns JSON identifying which backend was chosen.
    server {
        listen      127.0.0.1:%%PORT_8082%%;
        server_name backend;

        location /payments-eu/ {
            default_type application/json;
            return 200 '{"backend":"payments-eu"}';
        }
        location /payments-us/ {
            default_type application/json;
            return 200 '{"backend":"payments-us"}';
        }
        location /analytics/ {
            default_type application/json;
            return 200 '{"backend":"analytics"}';
        }
        location /default/ {
            default_type application/json;
            return 200 '{"backend":"default"}';
        }

        # Echo backend: returns the X-Forwarded-Body header value so
        # tests can verify the original body was forwarded.
        # Handler assigned by init.js.
        location /echo-fwd/ { }
    }

    # Router server — handler wired by init.js.
    server {
        listen      127.0.0.1:8080;
        server_name router;

        location /route/ { }

        location /internal/payments-eu/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/payments-eu/;
        }
        location /internal/payments-us/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/payments-us/;
        }
        location /internal/analytics/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/analytics/;
        }
        location /internal/default/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/default/;
        }
        location /internal/echo-fwd/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/echo-fwd/;
        }
    }
}
EOF

# -----------------------------------------------------------------------
# init.js
#   Part A — wire the /echo-fwd/ handler on the backend server
#   Part B — collectJsonPaths + routing handler on the router server
# -----------------------------------------------------------------------

$t->write_file('init.js', <<'JS');
(function () {
    'use strict';

    /* ==================================================================
     * Part A — backend server (servers[0]): /echo-fwd/ handler
     * Returns the X-Forwarded-Body header value so the test can verify
     * that the original request body was forwarded.
     * ================================================================== */

    (function () {
        var locs = nginx.http.servers[0].locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === '/echo-fwd/') {
                locs[i].handler = async function (req) {
                    var fwd = req.headers['x-forwarded-body'] || '{}';
                    req.respond(200,
                        { 'Content-Type': 'application/json' },
                        fwd);
                };
                break;
            }
        }
    }());

    /* ==================================================================
     * Part B — router server (servers[1])
     * ================================================================== */

    (function () {

        /* --------------------------------------------------------------
         * collectJsonPaths — partial JSON scanner
         * Stops as soon as every required path has been captured.
         * ------------------------------------------------------------ */

        function collectJsonPaths(json, paths) {
            var needed   = Object.create(null);
            var ancestor = Object.create(null);

            for (var i = 0; i < paths.length; i++) {
                needed[paths[i]] = true;
                var segs = paths[i].split('.');
                for (var d = 1; d < segs.length; d++) {
                    ancestor[segs.slice(0, d).join('.')] = true;
                }
            }

            var result = Object.create(null);
            var remain = paths.length;
            var pos    = 0;
            var stack  = [];
            var n      = json.length;

            function ws() {
                while (pos < n) {
                    var c = json[pos];
                    if (c !== ' ' && c !== '\t' && c !== '\r' && c !== '\n') { break; }
                    pos++;
                }
            }

            function readString() {
                pos++;
                var s = '';
                while (pos < n) {
                    var c = json[pos++];
                    if (c === '"') { break; }
                    if (c === '\\') {
                        var e = json[pos++];
                        s += (e === 'n' ? '\n' : e === 't' ? '\t' :
                              e === 'r' ? '\r' : e);
                    } else { s += c; }
                }
                return s;
            }

            function skipValue() {
                ws();
                if (pos >= n) { return; }
                var c = json[pos];
                if (c === '"') {
                    readString();
                } else if (c === '{' || c === '[') {
                    pos++;
                    var depth = 1;
                    while (pos < n && depth > 0) {
                        var ch = json[pos++];
                        if (ch === '"') {
                            while (pos < n) {
                                var q = json[pos++];
                                if (q === '\\') { pos++; }
                                else if (q === '"') { break; }
                            }
                        } else if (ch === '{' || ch === '[') { depth++; }
                        else if (ch === '}' || ch === ']')   { depth--; }
                    }
                } else {
                    while (pos < n) {
                        var t = json[pos];
                        if (t === ',' || t === '}' || t === ']' ||
                            t === ' ' || t === '\t' || t === '\r' || t === '\n') { break; }
                        pos++;
                    }
                }
            }

            function captureValue() {
                ws();
                if (pos >= n) { return undefined; }
                var c = json[pos];
                if (c === '"') { return readString(); }
                if (c === '{' || c === '[') {
                    var start = pos; skipValue(); return json.slice(start, pos);
                }
                var s = pos;
                while (pos < n) {
                    var t = json[pos];
                    if (t === ',' || t === '}' || t === ']' ||
                        t === ' ' || t === '\t' || t === '\r' || t === '\n') { break; }
                    pos++;
                }
                var raw = json.slice(s, pos);
                if (raw === 'true')  { return true;  }
                if (raw === 'false') { return false; }
                if (raw === 'null')  { return null;  }
                return +raw;
            }

            function walkObject() {
                pos++;
                ws();
                if (pos < n && json[pos] === '}') { pos++; return; }
                while (pos < n) {
                    ws();
                    if (pos >= n || json[pos] !== '"') { break; }
                    var key = readString();
                    ws();
                    if (pos < n && json[pos] === ':') { pos++; }
                    var cur = stack.length > 0
                        ? stack.join('.') + '.' + key : key;

                    if (needed[cur] && remain > 0) {
                        result[cur] = captureValue();
                        remain--;
                        if (remain === 0) { return; }
                    } else if (ancestor[cur]) {
                        ws();
                        if (pos < n && json[pos] === '{') {
                            stack.push(key);
                            walkObject();
                            stack.pop();
                        } else { skipValue(); }
                    } else {
                        skipValue();
                    }
                    if (remain === 0) { return; }
                    ws();
                    if (pos < n && json[pos] === '}') { pos++; return; }
                    if (pos < n && json[pos] === ',') { pos++; }
                }
            }

            ws();
            if (pos < n && json[pos] === '{') { walkObject(); }
            return result;
        }

        /* --------------------------------------------------------------
         * Routing table + pickBackend
         * ------------------------------------------------------------ */

        var ROUTING_PATHS = ['service', 'tenant.region'];

        var ROUTES = [
            { match: { 'service': 'payments', 'tenant.region': 'eu-west' },
              backend: '/internal/payments-eu/' },
            { match: { 'service': 'payments', 'tenant.region': 'us-east' },
              backend: '/internal/payments-us/' },
            { match: { 'service': 'analytics' },
              backend: '/internal/analytics/' },
            /* test-only route: echoes forwarded body back to the caller */
            { match: { 'service': 'echo' },
              backend: '/internal/echo-fwd/' },
        ];
        var DEFAULT_BACKEND = '/internal/default/';

        function pickBackend(fields) {
            for (var i = 0; i < ROUTES.length; i++) {
                var route = ROUTES[i];
                var match = true;
                var mkeys = Object.keys(route.match);
                for (var j = 0; j < mkeys.length; j++) {
                    if (fields[mkeys[j]] !== route.match[mkeys[j]]) {
                        match = false; break;
                    }
                }
                if (match) { return route.backend; }
            }
            return DEFAULT_BACKEND;
        }

        /* --------------------------------------------------------------
         * Content handler
         * ------------------------------------------------------------ */

        var routerSrv = nginx.http.servers[1];
        var routeLoc;
        var locs = routerSrv.locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === '/route/') { routeLoc = locs[i]; break; }
        }

        routeLoc.handler = async function (req) {
            var body;
            try { body = await req.readBody(); } catch (e) {
                req.respond(400, { 'Content-Type': 'application/json' },
                            '{"error":"could not read body"}\n'); return;
            }

            var trimmed = (body || '').trim();
            if (trimmed.length === 0) {
                req.respond(400, { 'Content-Type': 'application/json' },
                            '{"error":"empty body"}\n'); return;
            }
            if (trimmed[0] !== '{') {
                req.respond(400, { 'Content-Type': 'application/json' },
                            '{"error":"body must be a JSON object"}\n'); return;
            }

            var fields  = collectJsonPaths(trimmed, ROUTING_PATHS);
            var backend = pickBackend(fields);

            var sub;
            try {
                sub = await req.subrequest(backend, {
                    method:  req.method,
                    args:    req.args,
                    headers: {
                        'X-Forwarded-Body': body,
                        'Content-Type':
                            req.headers['content-type'] || 'application/json',
                    },
                });
            } catch (e) {
                req.respond(502, { 'Content-Type': 'application/json' },
                            '{"error":"backend unreachable"}\n'); return;
            }

            req.respond(sub.status, sub.headers, sub.body);
        };

    }());

}());
JS

$t->run();

# -----------------------------------------------------------------------
# Helper: send a POST to /route/ with a JSON body
# -----------------------------------------------------------------------

sub post_route {
    my ($body) = @_;
    my $len = length($body);
    return http(<<"EOF");
POST /route/ HTTP/1.0
Host: router
Content-Type: application/json
Content-Length: $len

$body
EOF
}

# -----------------------------------------------------------------------
# 1-2: service=payments + tenant.region=eu-west → payments-eu
# -----------------------------------------------------------------------

my $r = post_route('{"service":"payments","tenant":{"region":"eu-west"},"id":1}');
like($r, qr{200 OK},           'payments-eu: 200 OK');
like($r, qr{"backend":"payments-eu"}, 'payments-eu: routed to payments-eu');

# -----------------------------------------------------------------------
# 3-4: service=payments + tenant.region=us-east → payments-us
# -----------------------------------------------------------------------

$r = post_route('{"service":"payments","tenant":{"region":"us-east"},"id":2}');
like($r, qr{200 OK},           'payments-us: 200 OK');
like($r, qr{"backend":"payments-us"}, 'payments-us: routed to payments-us');

# -----------------------------------------------------------------------
# 5-6: service=analytics (region does not matter) → analytics
# -----------------------------------------------------------------------

$r = post_route('{"service":"analytics","tenant":{"region":"eu-west"},"id":3}');
like($r, qr{200 OK},         'analytics: 200 OK');
like($r, qr{"backend":"analytics"}, 'analytics: routed to analytics');

# -----------------------------------------------------------------------
# 7-8: Unknown service → default backend
# -----------------------------------------------------------------------

$r = post_route('{"service":"orders","tenant":{"region":"eu-west"},"id":4}');
like($r, qr{200 OK},       'default: 200 OK');
like($r, qr{"backend":"default"}, 'default: routed to default');

# -----------------------------------------------------------------------
# 9-10: Empty body → 400
# -----------------------------------------------------------------------

$r = http(<<'EOF');
POST /route/ HTTP/1.0
Host: router
Content-Type: application/json
Content-Length: 0

EOF
like($r,   qr{400},        'empty-body: 400');
like($r,   qr{"error":},   'empty-body: error field present');

# -----------------------------------------------------------------------
# 11-12: Non-JSON body → 400
# -----------------------------------------------------------------------

my $plain = 'not json at all';
$r = http(<<"EOF");
POST /route/ HTTP/1.0
Host: router
Content-Type: text/plain
Content-Length: ${\ length($plain) }

$plain
EOF
like($r,   qr{400},        'non-json: 400');
like($r,   qr{"error":},   'non-json: error field present');

# -----------------------------------------------------------------------
# 13-14: Large body — routing fields appear near the start; 4 KB of
# padding follows.  Verifies the early-exit path in collectJsonPaths:
# the scanner stops after finding 'service' and 'tenant.region' and
# never touches the padding.
# -----------------------------------------------------------------------

my $padding = 'x' x 4096;
my $large   = '{"service":"payments","tenant":{"region":"eu-west"},'
            . '"padding":"' . $padding . '"}';
$r = post_route($large);
like($r, qr{200 OK},                  'large-body: 200 OK despite 4 KB padding');
like($r, qr{"backend":"payments-eu"}, 'large-body: correctly routed to payments-eu');

# -----------------------------------------------------------------------
# 15: Body forwarding — original body reaches backend via X-Forwarded-Body.
# Routes to /internal/echo-fwd/ (service=echo), which echoes the header.
# -----------------------------------------------------------------------

my $marker = 'unique-marker-' . int(rand(999999));
$r = post_route('{"service":"echo","marker":"' . $marker . '"}');
like($r, qr{$marker}, 'body-fwd: unique marker echoed back from backend');
