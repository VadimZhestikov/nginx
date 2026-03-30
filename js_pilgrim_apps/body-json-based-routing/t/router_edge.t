#!/usr/bin/perl

# Edge-case tests for the Body JSON Router.
#
# Covers scenarios NOT in router.t:
#   1-2   Escape sequence in string value (\"quoted\") → routed correctly
#   3-4   Boolean service value (true) → no string match → default backend
#   5-6   Null service value → no string match → default backend
#   7-8   Numeric service value (42) → no string match → default backend
#   9-10  service field absent → default backend
#   11-12 service=payments but no tenant.region → default (no eu/us match)
#   13-14 Whitespace before opening '{' → still routed
#   15-16 JSON array body '[]' → 400 (not an object)
#   17-18 Deeply nested tenant (3 levels) with region → payments-eu
#   19    Escaped backslash in routing value ('pay\\\\ments') → default (no match)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(19);

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
    }

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
    }
}
EOF

# Same router logic as in router.t (inline for test isolation)
$t->write_file('init.js', <<'JS');
(function () {
    'use strict';

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

    var ROUTING_PATHS = ['service', 'tenant.region'];
    var ROUTES = [
        { match: { 'service': 'payments', 'tenant.region': 'eu-west' },
          backend: '/internal/payments-eu/' },
        { match: { 'service': 'payments', 'tenant.region': 'us-east' },
          backend: '/internal/payments-us/' },
        { match: { 'service': 'analytics' },
          backend: '/internal/analytics/' },
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

    var routerSrv = nginx.http.servers[1];
    var routeLoc;
    var locs = routerSrv.locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === '/route/') { routeLoc = locs[i]; break; }
    }

    routeLoc.handler = async function (req) {
        var scanner = createPathScanner(ROUTING_PATHS);
        var preTrim = req.bodyPreread.trim();
        if (preTrim.length > 0 && preTrim[0] === '{') {
            scanner.feed(preTrim);
            if (scanner.done()) {
                req.pass(pickBackend(scanner.getResult()));
                return;
            }
        }
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
        if (!sawJson) {
            req.respond(400, { 'Content-Type': 'application/json' },
                        '{"error":"body must be a JSON object"}\n'); return;
        }
        req.pass(pickBackend(scanner.getResult()));
    };

}());
JS

$t->run();

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
# 1-2: Escape sequence in string value: \"quoted\" inside service value
# The scanner's CE state decodes \", the result is the literal string
# with quotes.  No route matches → default backend.
# -----------------------------------------------------------------------

my $r = post_route('{"service":"pay\"ments","tenant":{"region":"eu-west"}}');
like($r, qr{200 OK},           'escape-val: 200 OK');
like($r, qr{"backend":"default"}, 'escape-val: escaped value → no match → default');

# -----------------------------------------------------------------------
# 3-4: Boolean service value — scanner stores JS true, not string "true"
# pickBackend compares with === so true !== 'payments' → default
# -----------------------------------------------------------------------

$r = post_route('{"service":true,"tenant":{"region":"eu-west"}}');
like($r, qr{200 OK},           'bool-val: 200 OK');
like($r, qr{"backend":"default"}, 'bool-val: boolean service → default');

# -----------------------------------------------------------------------
# 5-6: Null service value → default
# -----------------------------------------------------------------------

$r = post_route('{"service":null,"tenant":{"region":"eu-west"}}');
like($r, qr{200 OK},           'null-val: 200 OK');
like($r, qr{"backend":"default"}, 'null-val: null service → default');

# -----------------------------------------------------------------------
# 7-8: Numeric service value → default
# -----------------------------------------------------------------------

$r = post_route('{"service":42,"tenant":{"region":"eu-west"}}');
like($r, qr{200 OK},           'num-val: 200 OK');
like($r, qr{"backend":"default"}, 'num-val: numeric service → default');

# -----------------------------------------------------------------------
# 9-10: service field absent entirely → default
# -----------------------------------------------------------------------

$r = post_route('{"tenant":{"region":"eu-west"},"other":"x"}');
like($r, qr{200 OK},           'no-service: 200 OK');
like($r, qr{"backend":"default"}, 'no-service: missing service → default');

# -----------------------------------------------------------------------
# 11-12: service=payments but no tenant.region field → no eu/us match → default
# -----------------------------------------------------------------------

$r = post_route('{"service":"payments","other":"no-region"}');
like($r, qr{200 OK},           'no-region: 200 OK');
like($r, qr{"backend":"default"}, 'no-region: payments without region → default');

# -----------------------------------------------------------------------
# 13-14: Whitespace before '{' — scanner S state skips it
# -----------------------------------------------------------------------

my $ws_body = '   {"service":"analytics"}';
$r = post_route($ws_body);
like($r, qr{200 OK},                'ws-prefix: 200 OK');
like($r, qr{"backend":"analytics"}, 'ws-prefix: whitespace before { handled');

# -----------------------------------------------------------------------
# 15-16: JSON array body '[]' — sawJson stays false (not '{') → 400
# -----------------------------------------------------------------------

$r = post_route('["service","payments"]');
like($r, qr{400},      'array-body: 400');
like($r, qr{"error":}, 'array-body: error field present');

# -----------------------------------------------------------------------
# 17-18: Deeply nested JSON: extra wrapper object around tenant
# Verifies ancestor tracking descends into the correct object
# -----------------------------------------------------------------------

$r = post_route('{"service":"payments","wrapper":{"tenant":{"region":"eu-west"}},"tenant":{"region":"eu-west"}}');
like($r, qr{200 OK},                  'deep-nest: 200 OK');
like($r, qr{"backend":"payments-eu"}, 'deep-nest: correct route with nested objects present');

# -----------------------------------------------------------------------
# 19: Escaped backslash in routing value: service = "pay\\ments"
# The CE state stores '\\' as '\', so result is "pay\ments" ≠ "payments"
# → default backend.
# -----------------------------------------------------------------------

$r = post_route('{"service":"pay\\\\ments","tenant":{"region":"eu-west"}}');
like($r, qr{"backend":"default"}, 'escape-backslash: literal backslash in value → default');
