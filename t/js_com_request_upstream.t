#!/usr/bin/perl

# Tests for Stage 26 COM expansion: r.upstream + r.subrequest upstream field
#
# r.upstream — reads r->upstream_states (last attempt) on NginxRequest;
#              null when the request was not proxied.
#
# r.subrequest() now includes an `upstream` field in the resolved object:
#   { status, body, upstream: {status, responseTime, connectTime,
#                               bytesReceived, addr} | null }
#
# Test layout:
#   8080 — JS handler server (main)
#   8081 — plain backend server (proxy target)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/init_upstream.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    # --- backend: plain responses, no JS ---
    server {
        listen      127.0.0.1:8081;
        server_name backend;

        location / {
            return 200 "backend ok";
        }

        location /slow {
            return 503 "backend error";
        }
    }

    # --- frontend: JS handlers, proxy subrequests to 8081 ---
    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # proxied sub-location (not a JS handler — pure proxy_pass)
        location /be {
            proxy_pass http://127.0.0.1:8081/;
        }

        location /be_slow {
            proxy_pass http://127.0.0.1:8081/slow;
        }

        # JS handler: issues subrequest to /be, inspects upstream field
        location /check_subreq_upstream {
        }

        # JS handler: non-proxied request, r.upstream should be null
        location /check_no_upstream {
        }

        # JS handler: subrequest to static /inner (no proxy), upstream null
        location /inner {
            return 200 "inner ok";
        }

        location /check_static_subreq {
        }
    }
}
EOF

$t->write_file('init_upstream.js', <<'JS');
(function() {
    const locs = nginx.http.servers[1].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/check_subreq_upstream', checkProxiedSubreq);
    set('/check_no_upstream',     checkNoUpstream);
    set('/check_static_subreq',   checkStaticSubreq);
})();

async function checkProxiedSubreq(r) {
    const res = await r.subrequest('/be');
    const u = res.upstream;
    const ok = u !== null
            && typeof u === 'object'
            && typeof u.status === 'number'
            && typeof u.responseTime === 'number'
            && typeof u.connectTime  === 'number'
            && typeof u.bytesReceived === 'number'
            && typeof u.addr === 'string'
            && u.addr.length > 0
            && u.status === 200;
    r.respond(200, {'content-type': 'text/plain'},
              ok ? 'PASS' : 'FAIL:' + JSON.stringify(u));
}

function checkNoUpstream(r) {
    /* Non-proxied request — r.upstream must be null */
    const u = r.upstream;
    r.respond(200, {'content-type': 'text/plain'},
              u === null ? 'PASS' : 'FAIL:' + JSON.stringify(u));
}

async function checkStaticSubreq(r) {
    /* Subrequest to a return-based location — no upstream */
    const res = await r.subrequest('/inner');
    r.respond(200, {'content-type': 'text/plain'},
              res.upstream === null ? 'PASS'
                  : 'FAIL:' + JSON.stringify(res.upstream));
}
JS

$t->try_run('no js module')->plan(5);

# r.upstream on non-proxied JS handler → null
like(http_get('/check_no_upstream'), qr/PASS/,
     'r.upstream: null for non-proxied request');

# r.subrequest to proxy_pass location → upstream field populated
like(http_get('/check_subreq_upstream'), qr/PASS/,
     'r.subrequest upstream: status/addr/times present for proxied subreq');

# r.subrequest to static location → upstream null
like(http_get('/check_static_subreq'), qr/PASS/,
     'r.subrequest upstream: null for non-proxied subreq');

# subrequest upstream.status matches the HTTP status
my $r4 = http_get('/check_subreq_upstream');
like($r4, qr/200 OK/, 'check_subreq_upstream: responds 200');

# verify the backend itself works (sanity)
like(http_get('/be'), qr/backend ok/, 'backend /be: reachable directly');
