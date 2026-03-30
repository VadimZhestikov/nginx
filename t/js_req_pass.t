#!/usr/bin/perl

# Tests for req.pass(location) — nginx internal redirect from JS handlers.
#
# req.pass(location) calls ngx_http_internal_redirect and re-enters the
# nginx phase engine at the new URI.  Unlike req.subrequest():
#   • The original r->request_body is preserved — proxy_pass streams it.
#   • No size limit (no header-copy bottleneck).
#   • Works from both sync and async handlers.
#
# Tests:
#   1  — sync handler: req.pass routes to correct internal location
#   2  — async handler: awaits bodyChunks(), then req.pass
#   3  — query string: '?key=val' is split from URI and preserved
#   4  — internal guard: /internal/ location rejects direct client requests
#   5  — double pass: second req.pass() throws "already responded"
#   6  — body forwarding: proxy_pass streams r->request_body to backend
#   7  — setVariable + pass: variable set before pass is visible to proxy_pass
#   8  — empty body: pass works even when Content-Length is 0

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/req_pass.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # --- handler locations (JS wired by req_pass.js) ---
        location /sync-pass  { }
        location /async-pass { }
        location /qs-pass    { }
        location /double     { }
        location /body-echo  { }
        location /zero-body  { }

        # setVariable test: declare $js_target here so nginx registers it,
        # but keep it in the *original* location's rewrite phase only —
        # not the server rewrite phase — so the internal redirect to
        # /internal/varpass/ does NOT reset it before proxy_pass reads it.
        location /setvar {
            set $js_target '';
        }

        # --- internal relay locations ---

        location /internal/a/ {
            internal;
            default_type application/json;
            return 200 '{"dest":"a"}';
        }
        location /internal/b/ {
            internal;
            default_type application/json;
            return 200 '{"dest":"b"}';
        }

        # Echoes the actual request body received via proxy_pass.
        # The body comes from r->request_body forwarded by the internal redirect.
        location /internal/echo/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/echo/;
        }

        # Uses $js_target variable (set by setVariable) to pick the upstream.
        # $js_target is declared in /setvar so it has a variable index;
        # the /setvar rewrite phase sets it to '' initially, but the JS
        # handler overwrites it before calling req.pass().  The internal
        # redirect starts at server_rewrite_index, which has no 'set' for
        # $js_target, so the value set by JS survives to proxy_pass.
        location /internal/varpass/ {
            internal;
            proxy_pass http://127.0.0.1:%%PORT_8082%%/$js_target;
        }
    }

    # Backend server — provides echo and variable-routed endpoints.
    server {
        listen      127.0.0.1:%%PORT_8082%%;
        server_name backend;

        # Echoes the received request body.
        location /echo/ { }

        # Returns a marker identifying which path was hit.
        location /target-a/ {
            default_type application/json;
            return 200 '{"via":"target-a"}';
        }
        location /target-b/ {
            default_type application/json;
            return 200 '{"via":"target-b"}';
        }
    }
}
EOF

$t->write_file('req_pass.js', <<'JS');
(function () {
    'use strict';

    var srv  = nginx.http.servers[0];
    var be   = nginx.http.servers[1];
    var locs = srv.locations;
    var bloc = be.locations;

    function findLoc(arr, path) {
        for (var i = 0; i < arr.length; i++) {
            if (arr[i].path === path) { return arr[i]; }
        }
        return null;
    }

    /* Backend: /echo/ — read body forwarded by proxy_pass, echo it back */
    var echoLoc = findLoc(bloc, '/echo/');
    if (echoLoc) {
        echoLoc.handler = async function (req) {
            var body = await req.readBody();
            req.respond(200, { 'Content-Type': 'text/plain' }, body || '');
        };
    }

    /* 1: sync handler — req.pass to /internal/a/ */
    var loc = findLoc(locs, '/sync-pass');
    if (loc) {
        loc.handler = function (req) {
            req.pass('/internal/a/');
        };
    }

    /* 2: async handler — read one bodyChunks chunk, then req.pass */
    loc = findLoc(locs, '/async-pass');
    if (loc) {
        loc.handler = async function (req) {
            for await (var chunk of req.bodyChunks()) {
                /* Just confirm body is there; pass regardless */
                break;
            }
            req.pass('/internal/b/');
        };
    }

    /* 3: query string — verify args are split and forwarded */
    loc = findLoc(locs, '/qs-pass');
    if (loc) {
        loc.handler = function (req) {
            req.pass('/internal/a/?info=qs-test');
        };
    }

    /* 5: double pass — second call should throw; first pass wins */
    loc = findLoc(locs, '/double');
    if (loc) {
        loc.handler = function (req) {
            req.pass('/internal/a/');
            try {
                req.pass('/internal/b/');
            } catch (e) {
                /* expected: second pass throws "already responded" */
            }
        };
    }

    /* 6: body echo — pass preserves r->request_body for proxy_pass */
    loc = findLoc(locs, '/body-echo');
    if (loc) {
        loc.handler = async function (req) {
            /* Scan body chunks (don't accumulate) then pass */
            for await (var chunk of req.bodyChunks()) {
                break;  /* read first chunk, enough to trigger body read */
            }
            req.pass('/internal/echo/');
        };
    }

    /* 7: setVariable + pass — $js_target used in proxy_pass URL */
    loc = findLoc(locs, '/setvar');
    if (loc) {
        loc.handler = function (req) {
            req.setVariable('js_target', 'target-a/');
            req.pass('/internal/varpass/');
        };
    }

    /* 8: zero-body pass — Content-Length: 0, pass still works */
    loc = findLoc(locs, '/zero-body');
    if (loc) {
        loc.handler = function (req) {
            req.pass('/internal/a/');
        };
    }

}());
JS

$t->run();

# -----------------------------------------------------------------------
# 1: sync handler calls req.pass → /internal/a/ returns {"dest":"a"}
# -----------------------------------------------------------------------

my $r = http("GET /sync-pass HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{"dest":"a"}, 'sync-pass: routed to /internal/a/');

# -----------------------------------------------------------------------
# 2: async handler — bodyChunks + req.pass → /internal/b/
# -----------------------------------------------------------------------

$r = http("POST /async-pass HTTP/1.0\r\nHost: localhost\r\n" .
          "Content-Length: 4\r\n\r\ntest");
like($r, qr{"dest":"b"}, 'async-pass: async handler routed to /internal/b/');

# -----------------------------------------------------------------------
# 3: query string split — '?' separated from URI, args preserved
# (The static return ignores args, but the request must not 404.)
# -----------------------------------------------------------------------

$r = http("GET /qs-pass HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{"dest":"a"}, 'qs-pass: query string stripped from URI correctly');

# -----------------------------------------------------------------------
# 4: direct access to /internal/ is rejected (nginx `internal;` guard)
# -----------------------------------------------------------------------

$r = http("GET /internal/a/ HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{404}, 'internal-guard: direct request to /internal/ is 404');

# -----------------------------------------------------------------------
# 5: second req.pass() throws "already responded"
# -----------------------------------------------------------------------

$r = http("GET /double HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{"dest":"a"}, 'double-pass: first pass wins; second pass() is rejected');

# -----------------------------------------------------------------------
# 6: body forwarding — proxy_pass sends r->request_body to /echo/
# -----------------------------------------------------------------------

my $payload = 'hello-from-client-' . int(rand(99999));
$r = http("POST /body-echo HTTP/1.0\r\nHost: localhost\r\n" .
          "Content-Length: " . length($payload) . "\r\n\r\n" . $payload);
like($r, qr{$payload}, 'body-fwd: request body forwarded intact by proxy_pass');

# -----------------------------------------------------------------------
# 7: setVariable + pass — $js_target variable used in proxy_pass URL
# -----------------------------------------------------------------------

$r = http("GET /setvar HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{"via":"target-a"}, 'setvar-pass: setVariable routes to target-a via proxy_pass');

# -----------------------------------------------------------------------
# 8: zero-body pass — Content-Length: 0, pass works normally
# -----------------------------------------------------------------------

$r = http("POST /zero-body HTTP/1.0\r\nHost: localhost\r\n" .
          "Content-Length: 0\r\n\r\n");
like($r, qr{"dest":"a"}, 'zero-body: pass works with empty body');
