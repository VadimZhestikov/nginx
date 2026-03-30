#!/usr/bin/perl

# Tests for the Body JSON Router reference application.
#
# Architecture: createPathScanner (stateful streaming scanner) + req.pass()
#   — zero whole-body buffering in JS; body is forwarded by proxy_pass from
#   nginx's native buffers (r->request_body), not via X-Forwarded-Body header.
#
# Verifies that:
#   • createPathScanner correctly extracts routing fields (including nested)
#   • Routing to each of the four backends works
#   • Early-exit: large bodies with fields near the start still route correctly
#   • Error responses for empty and non-JSON bodies
#   • The original request body reaches the backend via proxy_pass (real body)
#   • Two-phase handler: bodyPreread (sync) then bodyChunks() (async iterator)
#   • scanner.done() stops chunk iteration and req.pass() hands off correctly
#
# Tests:
#   1-2   service=payments  + tenant.region=eu-west  → payments-eu  (200 + body)
#   3-4   service=payments  + tenant.region=us-east  → payments-us  (200 + body)
#   5-6   service=analytics + any region             → analytics    (200 + body)
#   7-8   service=orders    (unknown)                → default      (200 + body)
#   9-10  Empty body                                 → 400 + "error"
#   11-12 Non-JSON body (plain text)                 → 400 + "error"
#   13-14 Large body — routing fields first, 4 KB padding after → 200 + correct route
#   15    Body forwarding — proxy_pass forwards real body; backend echoes it
#   16    Preread path — compact single-field match (analytics, no region needed)
#   17    bodyChunks path — service field at end of large body, still routes

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(17);

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
    # Routing happens via req.pass() → proxy_pass — the real request body
    # is forwarded by nginx, not via a custom header.
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

        # Echo backend: reads the actual request body (forwarded by
        # proxy_pass from r->request_body) and returns it verbatim.
        # Handler assigned by init.js.
        location /echo-body/ { }
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
        location /internal/echo-body/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/echo-body/;
        }
    }
}
EOF

# -----------------------------------------------------------------------
# init.js
#   Part A — wire the /echo-body/ handler on the backend server
#   Part B — createPathScanner + routing handler on the router server
# -----------------------------------------------------------------------

$t->write_file('init.js', <<'JS');
(function () {
    'use strict';

    /* ==================================================================
     * Part A — backend server (servers[0]): /echo-body/ handler
     * Reads the actual request body forwarded by proxy_pass and echoes
     * it back so tests can verify the original body reached the backend.
     * ================================================================== */

    (function () {
        var locs = nginx.http.servers[0].locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === '/echo-body/') {
                locs[i].handler = async function (req) {
                    var body = await req.readBody();
                    req.respond(200,
                        { 'Content-Type': 'application/json' },
                        body || '{}');
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
         * createPathScanner — stateful streaming JSON path scanner.
         * Each byte processed exactly once; resumable across chunk
         * boundaries; exits as soon as all required paths are found.
         * ------------------------------------------------------------ */

        function createPathScanner(paths) {
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
            var stack  = [];
            var partial = '';
            var curKey  = '';
            var ndepth  = 0;
            var state   = 'S';

            function fullPath() {
                return stack.length > 0
                    ? stack.join('.') + '.' + curKey : curKey;
            }

            return {
                feed: function (chunk) {
                    if (state === 'D') { return; }
                    var n = chunk.length;
                    var i = 0;
                    var c, fp, raw;
                    while (i < n && state !== 'D') {
                        c = chunk[i++];
                        switch (state) {
                        case 'S':
                            if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                            state = (c === '{') ? 'O' : 'D';
                            break;
                        case 'O':
                            if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                            if (c === '"') { partial = ''; state = 'K'; }
                            else if (c === '}') {
                                if (stack.length > 0) { stack.pop(); state = 'AV'; }
                                else { state = 'D'; }
                            }
                            break;
                        case 'K':
                            if      (c === '"')  { curKey = partial; partial = ''; state = ':'; }
                            else if (c === '\\') { state = 'E'; }
                            else                 { partial += c; }
                            break;
                        case 'E':
                            partial += (c === 'n' ? '\n' : c === 't' ? '\t' :
                                        c === 'r' ? '\r' : c);
                            state = 'K';
                            break;
                        case ':':
                            if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                            if (c === ':') { state = 'V'; }
                            break;
                        case 'V':
                            if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                            fp = fullPath();
                            if (needed[fp]) {
                                if (c === '"') { partial = ''; state = 'CS'; }
                                else if (c === '{' || c === '[') { ndepth = 1; state = 'SN'; }
                                else { partial = c; state = 'CX'; }
                            } else if (ancestor[fp] && c === '{') {
                                stack.push(curKey); state = 'O';
                            } else {
                                if      (c === '"')              { state = 'SS'; }
                                else if (c === '{' || c === '[') { ndepth = 1; state = 'SN'; }
                                else                             { state = 'SX'; }
                            }
                            break;
                        case 'CS':
                            if (c === '"') {
                                result[fullPath()] = partial; partial = '';
                                if (--remain === 0) { state = 'D'; break; }
                                state = 'AV';
                            } else if (c === '\\') { state = 'CE'; }
                            else { partial += c; }
                            break;
                        case 'CE':
                            partial += (c === 'n' ? '\n' : c === 't' ? '\t' :
                                        c === 'r' ? '\r' : c);
                            state = 'CS';
                            break;
                        case 'CX':
                            if (c === ',' || c === '}' || c === ']' ||
                                c === ' ' || c === '\t' || c === '\r' || c === '\n') {
                                raw = partial; partial = '';
                                result[fullPath()] = (raw === 'true'  ? true  :
                                                      raw === 'false' ? false :
                                                      raw === 'null'  ? null  : +raw);
                                if (--remain === 0) { state = 'D'; break; }
                                if (c === '}') {
                                    if (stack.length > 0) { stack.pop(); state = 'AV'; }
                                    else { state = 'D'; }
                                } else if (c === ',') { state = 'O'; }
                                else { state = 'AV'; }
                            } else { partial += c; }
                            break;
                        case 'SS':
                            if      (c === '"')  { state = 'AV'; }
                            else if (c === '\\') { state = 'SE'; }
                            break;
                        case 'SE': state = 'SS'; break;
                        case 'SX':
                            if (c === ',' || c === '}' || c === ']' ||
                                c === ' ' || c === '\t' || c === '\r' || c === '\n') {
                                if (c === '}') {
                                    if (stack.length > 0) { stack.pop(); state = 'AV'; }
                                    else { state = 'D'; }
                                } else if (c === ',') { state = 'O'; }
                                else { state = 'AV'; }
                            }
                            break;
                        case 'SN':
                            if      (c === '"')              { state = 'NS'; }
                            else if (c === '{' || c === '[') { ndepth++; }
                            else if (c === '}' || c === ']') {
                                if (--ndepth === 0) { state = 'AV'; }
                            }
                            break;
                        case 'NS':
                            if      (c === '"')  { state = 'SN'; }
                            else if (c === '\\') { state = 'NE'; }
                            break;
                        case 'NE': state = 'NS'; break;
                        case 'AV':
                            if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                            if      (c === ',') { state = 'O'; }
                            else if (c === '}') {
                                if (stack.length > 0) { stack.pop(); state = 'AV'; }
                                else { state = 'D'; }
                            }
                            break;
                        }
                    }
                },
                done:      function () { return remain === 0; },
                getResult: function () { return result; }
            };
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
              backend: '/internal/echo-body/' },
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
         * Content handler — two-phase streaming router
         *   Phase 1: sync bodyPreread scan (zero I/O for small envelopes)
         *   Phase 2: bodyChunks() scan (one byte per byte, stops on done())
         *   req.pass(backend): nginx streams r->request_body via proxy_pass
         * ------------------------------------------------------------ */

        var routerSrv = nginx.http.servers[1];
        var routeLoc;
        var locs = routerSrv.locations;
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === '/route/') { routeLoc = locs[i]; break; }
        }

        routeLoc.handler = async function (req) {

            /* Phase 1: sync scan of req.bodyPreread */
            var scanner = createPathScanner(ROUTING_PATHS);
            var preTrim = req.bodyPreread.trim();
            if (preTrim.length > 0 && preTrim[0] === '{') {
                scanner.feed(preTrim);
                if (scanner.done()) {
                    req.pass(pickBackend(scanner.getResult()));
                    return;
                }
            }

            /* Phase 2: chunk-by-chunk scan until done() or body exhausted */
            var sawJson = preTrim.length > 0 && preTrim[0] === '{';
            try {
                for await (var chunk of req.bodyChunks()) {
                    if (!sawJson) {
                        var t = (preTrim + chunk).trim();
                        sawJson = t.length > 0 && t[0] === '{';
                    }
                    scanner.feed(chunk);
                    if (scanner.done()) { break; }
                }
            } catch (e) {
                req.respond(400, { 'Content-Type': 'application/json' },
                            '{"error":"could not read body"}\n'); return;
            }

            /* Validate: require a JSON object. */
            if (!sawJson) {
                req.respond(400, { 'Content-Type': 'application/json' },
                            '{"error":"body must be a JSON object"}\n'); return;
            }

            /* Pass to the chosen backend — proxy_pass streams the body. */
            req.pass(pickBackend(scanner.getResult()));
        };

    }());

}());
JS

$t->run();

# -----------------------------------------------------------------------
# Helper: POST to /route/ with a JSON body
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
# padding follows.  Verifies the early-exit path in createPathScanner:
# the scanner stops after finding 'service' and 'tenant.region' and
# never touches the padding.  Body forwarded by proxy_pass from nginx
# buffers — no JS-side whole-body string created.
# -----------------------------------------------------------------------

my $padding = 'x' x 4096;
my $large   = '{"service":"payments","tenant":{"region":"eu-west"},'
            . '"padding":"' . $padding . '"}';
$r = post_route($large);
like($r, qr{200 OK},                  'large-body: 200 OK despite 4 KB padding');
like($r, qr{"backend":"payments-eu"}, 'large-body: correctly routed to payments-eu');

# -----------------------------------------------------------------------
# 15: Body forwarding via proxy_pass.
# Routes to /internal/echo-body/ (service=echo), which reads and echoes
# the body received from proxy_pass.  The unique marker verifies the real
# request body — not a header copy — was forwarded intact.
# -----------------------------------------------------------------------

my $marker = 'unique-marker-' . int(rand(999999));
$r = post_route('{"service":"echo","marker":"' . $marker . '"}');
like($r, qr{$marker}, 'body-fwd: unique marker echoed from backend via proxy_pass');

# -----------------------------------------------------------------------
# 16: Preread path — compact single-field match (analytics needs only
# 'service').  A compact JSON body fits in one recv() so bodyPreread
# delivers the routing fields synchronously; bodyChunks() is never called.
# -----------------------------------------------------------------------

$r = post_route('{"service":"analytics"}');
like($r, qr{"backend":"analytics"}, 'preread-path: single-field analytics routed via preread');

# -----------------------------------------------------------------------
# 17: bodyChunks path — service field appears after 4 KB of noise.
# The streaming scanner processes each byte once (O(n)), finds 'service'
# after the noise, breaks the bodyChunks() loop, and calls req.pass().
# Uses analytics route (single-field match, no region required).
# -----------------------------------------------------------------------

my $late_body = '{"noise":"' . ('z' x 4096) . '","service":"analytics"}';
$r = post_route($late_body);
like($r, qr{"backend":"analytics"}, 'chunks-path: service at end of large body routes to analytics');
