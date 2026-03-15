#!/usr/bin/perl

# Tests for Phase 4 — per-request snapshot write/read mode extended to
# sub-object COM classes (NginxProxy, NginxGzip, NginxHeaders, NginxRewrite).
#
# When r.location.setWriteMode("local") is called, subsequent writes to any
# sub-object (location.proxy, .gzip, .headers, .rewrite) should go to a
# per-request snapshot and not affect the shared global config.
#
# Test structure:
#   /mutate_proxy    — sets writeMode=local, changes proxy.connectTimeout,
#                      reads back local value; second request sees original
#   /mutate_gzip     — sets writeMode=local, changes gzip.enable,
#                      reads back local value; second request sees original
#   /mutate_headers  — sets writeMode=local, changes headers.headersInherit,
#                      reads back local value; second request sees original
#   /mutate_rewrite  — sets writeMode=local, changes rewrite.log,
#                      reads back local value; second request sees original
#   /read_global     — no mutation; reads property from global struct

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy rewrite/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/sub_obj_handlers.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        # proxy.connectTimeout starts at 60000 ms (nginx default)
        location /mutate_proxy {
            proxy_pass         http://127.0.0.1:9999;
            proxy_connect_timeout  60s;
        }

        # gzip.enable starts as "off"
        location /mutate_gzip {
            gzip off;
        }

        # headers.headersInherit starts as "off"
        location /mutate_headers {
        }

        # rewrite.log starts as "off" (default)
        location /mutate_rewrite {
        }

        # plain read — used to verify global values are still pristine
        location /read_global_proxy {
            proxy_pass         http://127.0.0.1:9999;
            proxy_connect_timeout  60s;
        }

        location /read_global_gzip {
            gzip off;
        }

        location /read_global_rewrite {
        }
    }
}
EOF

$t->write_file('sub_obj_handlers.js', <<'JS');
(function installHandlers() {
    const srv  = nginx.http.servers[0];
    const locs = srv.locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    // /mutate_proxy — write local only; read back local value
    set('/mutate_proxy', function(r) {
        const loc   = r.location;
        const proxy = loc.proxy;

        // sanity: before mutate, original is 60000 ms
        const before = proxy.connectTimeout;

        loc.setWriteMode("local");
        proxy.connectTimeout = 1234;

        // read back — default read_mode is global; use getProperty on location
        // but for proxy sub-object we use setReadMode("local")
        loc.setReadMode("local");
        const local_v = proxy.connectTimeout;

        // global should still be 60000
        loc.setReadMode("global");
        const global_v = proxy.connectTimeout;

        r.respond(200, {}, before + "," + local_v + "," + global_v);
    });

    // /mutate_gzip — write local only; read back local value
    set('/mutate_gzip', function(r) {
        const loc  = r.location;
        const gzip = loc.gzip;
        if (gzip === null) {
            r.respond(200, {}, "no-gzip");
            return;
        }

        const before = gzip.enable;

        loc.setWriteMode("local");
        gzip.enable = true;

        loc.setReadMode("local");
        const local_v = gzip.enable;

        loc.setReadMode("global");
        const global_v = gzip.enable;

        r.respond(200, {}, before + "," + local_v + "," + global_v);
    });

    // /mutate_headers — write local only; change headersInherit
    // Default is "on"; mutate to "merge" locally.
    set('/mutate_headers', function(r) {
        const loc     = r.location;
        const headers = loc.headers;

        const before = headers.headersInherit;

        loc.setWriteMode("local");
        headers.headersInherit = "merge";

        loc.setReadMode("local");
        const local_v = headers.headersInherit;

        loc.setReadMode("global");
        const global_v = headers.headersInherit;

        r.respond(200, {}, before + "," + local_v + "," + global_v);
    });

    // /mutate_rewrite — write local only; change rewrite.log
    set('/mutate_rewrite', function(r) {
        const loc     = r.location;
        const rewrite = loc.rewrite;

        const before = rewrite.log;

        loc.setWriteMode("local");
        rewrite.log = true;

        loc.setReadMode("local");
        const local_v = rewrite.log;

        loc.setReadMode("global");
        const global_v = rewrite.log;

        r.respond(200, {}, before + "," + local_v + "," + global_v);
    });

    // /read_global_proxy — verify original connectTimeout is still 60000
    set('/read_global_proxy', function(r) {
        const v = r.location.proxy.connectTimeout;
        r.respond(200, {}, String(v));
    });

    // /read_global_gzip — verify gzip.enable is still false
    set('/read_global_gzip', function(r) {
        const g = r.location.gzip;
        r.respond(200, {}, g ? String(g.enable) : "no-gzip");
    });

    // /read_global_rewrite — verify rewrite.log is still false
    set('/read_global_rewrite', function(r) {
        r.respond(200, {}, String(r.location.rewrite.log));
    });
}());
JS

$t->try_run('no js module')->plan(18);

# /mutate_proxy: connectTimeout local mutation is isolated
my $r = http_get('/mutate_proxy');
like($r, qr/200/, '/mutate_proxy responds 200');
like($r, qr/60000,1234,60000/,
     '/mutate_proxy: before=60000, local=1234, global=60000');

# After the mutating request, a fresh request should see original 60000
$r = http_get('/read_global_proxy');
like($r, qr/200/, '/read_global_proxy responds 200');
like($r, qr/60000/, '/read_global_proxy: global connectTimeout still 60000');

# /mutate_gzip: gzip.enable local mutation is isolated
$r = http_get('/mutate_gzip');
like($r, qr/200/, '/mutate_gzip responds 200');
like($r, qr/false,true,false/,
     '/mutate_gzip: before=false, local=true, global=false');

# After the mutating request, global gzip.enable is still false
$r = http_get('/read_global_gzip');
like($r, qr/200/, '/read_global_gzip responds 200');
like($r, qr/false/, '/read_global_gzip: gzip.enable still false globally');

# /mutate_headers: headersInherit local mutation is isolated
# Default headersInherit is "on"; mutate to "merge" locally
$r = http_get('/mutate_headers');
like($r, qr/200/, '/mutate_headers responds 200');
like($r, qr/on,merge,on/,
     '/mutate_headers: before=on, local=merge, global=on');

# /mutate_rewrite: rewrite.log local mutation is isolated
$r = http_get('/mutate_rewrite');
like($r, qr/200/, '/mutate_rewrite responds 200');
like($r, qr/false,true,false/,
     '/mutate_rewrite: before=false, local=true, global=false');

# After the mutating request, global rewrite.log is still false
$r = http_get('/read_global_rewrite');
like($r, qr/200/, '/read_global_rewrite responds 200');
like($r, qr/false/, '/read_global_rewrite: rewrite.log still false globally');

# Second independent request to /mutate_proxy sees original 60000 still
$r = http_get('/mutate_proxy');
like($r, qr/60000,1234,60000/,
     '/mutate_proxy second request: global untouched, still 60000');

# Verify idempotence: second /mutate_gzip also sees original false
$r = http_get('/mutate_gzip');
like($r, qr/false,true,false/,
     '/mutate_gzip second request: global still false');

# Verify headers idempotence
$r = http_get('/mutate_headers');
like($r, qr/on,merge,on/,
     '/mutate_headers second request: global still on');

# Verify rewrite idempotence
$r = http_get('/mutate_rewrite');
like($r, qr/false,true,false/,
     '/mutate_rewrite second request: global still false');

# Auto-check: nginx started without errors (covered by try_run above)
# Auto-check: plan count matches
