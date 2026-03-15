#!/usr/bin/perl

# Tests for Phase 2+3 — per-request loc_conf snapshot with read/write modes.
#
# NginxLocation gains:
#   setWriteMode("global"|"local"|"both")  — where '=' assignments go
#   setReadMode("global"|"local")          — where getters read from
#   setProperty(name, value[, mode])       — explicit write with mode override
#   getProperty(name[, mode])              — explicit read with mode override
#
# Default behaviour (write_mode="both", read_mode="local"):
#   * loc.sendfile = v  — writes to global AND per-request snapshot
#   * loc.sendfile      — reads from snapshot (=global too since WRITE_BOTH
#                         wrote both identical values)
#
# WRITE_LOCAL:
#   * Only the snapshot is modified; the global struct is untouched.
#   * A second concurrent request sees the original global value.
#
# WRITE_GLOBAL:
#   * Only the global struct is modified (old pre-Phase2 behaviour).
#   * The snapshot (if any) is not touched.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/rw_mode_handlers.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        # Initial sendfile value is set from the nginx.conf default (off)
        sendfile off;

        location /read_local  { }
        location /read_global { }
        location /write_local { }
        location /write_both  { }
        location /set_prop    { }
        location /get_prop    { }
    }
}
EOF

$t->write_file('rw_mode_handlers.js', <<'JS');
(function installHandlers() {
    const srv  = nginx.http.servers[0];
    const locs = srv.locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    // /read_local — write via BOTH then read via LOCAL (should see written value)
    set('/read_local', function(r) {
        const loc = r.location;
        loc.setWriteMode("both");
        loc.sendfile = true;
        // read_mode defaults to "global" — WRITE_BOTH also wrote global, so true
        r.respond(200, {}, String(loc.sendfile));
    });

    // /read_global — write LOCAL only, then read GLOBAL (should see original)
    set('/read_global', function(r) {
        const loc = r.location;
        loc.setWriteMode("local");
        loc.sendfile = true;
        // explicitly read from global — should still be false (nginx.conf default)
        const v = loc.getProperty("sendfile", "global");
        r.respond(200, {}, String(v));
    });

    // /write_local — write LOCAL sendfile=true; verify global unchanged via
    //               a getProperty("sendfile","global") call on same request
    set('/write_local', function(r) {
        const loc = r.location;
        const before = loc.getProperty("sendfile", "global");
        loc.setWriteMode("local");
        loc.sendfile = true;
        const local_v  = loc.getProperty("sendfile", "local");
        const global_v = loc.getProperty("sendfile", "global");
        r.respond(200, {}, before + "," + local_v + "," + global_v);
    });

    // /write_both — write BOTH sendfile=true; both local and global should be true
    set('/write_both', function(r) {
        const loc = r.location;
        loc.setWriteMode("both");
        loc.sendfile = true;
        const local_v  = loc.getProperty("sendfile", "local");
        const global_v = loc.getProperty("sendfile", "global");
        r.respond(200, {}, local_v + "," + global_v);
    });

    // /set_prop — use setProperty with explicit mode
    // logNotFound defaults to true; write false locally → local=false, global=true
    set('/set_prop', function(r) {
        const loc = r.location;
        loc.setProperty("logNotFound", false, "local");
        const local_v  = loc.getProperty("logNotFound", "local");
        const global_v = loc.getProperty("logNotFound", "global");
        r.respond(200, {}, local_v + "," + global_v);
    });

    // /get_prop — getProperty with mode override
    set('/get_prop', function(r) {
        const loc = r.location;
        // Write local only then read back with explicit modes
        loc.setProperty("chunkedTransferEncoding", false, "local");
        const v_local  = loc.getProperty("chunkedTransferEncoding", "local");
        const v_global = loc.getProperty("chunkedTransferEncoding", "global");
        r.respond(200, {}, v_local + "," + v_global);
    });
}());
JS

$t->try_run('no js module')->plan(12);

# /read_local: WRITE_BOTH + READ_LOCAL → sees written value
my $r = http_get('/read_local');
like($r, qr/200/, '/read_local responds 200');
like($r, qr/true/, '/read_local: local read sees written value');

# /read_global: WRITE_LOCAL only + getProperty("global") → original value
$r = http_get('/read_global');
like($r, qr/200/, '/read_global responds 200');
like($r, qr/false/, '/read_global: global read sees original value');

# /write_local: WRITE_LOCAL → local=true, global=false (unchanged)
$r = http_get('/write_local');
like($r, qr/200/, '/write_local responds 200');
like($r, qr/false,true,false/, '/write_local: before=false,local=true,global=false');

# /write_both: WRITE_BOTH → local=true, global=true
$r = http_get('/write_both');
like($r, qr/200/, '/write_both responds 200');
like($r, qr/true,true/, '/write_both: both local and global are true');

# /set_prop: setProperty("logNotFound", false, local) → local=false, global=true (nginx default)
$r = http_get('/set_prop');
like($r, qr/200/, '/set_prop responds 200');
like($r, qr/false,true/, '/set_prop: local=false, global=true');

# /get_prop: setProperty("chunkedTransferEncoding", false, local)
$r = http_get('/get_prop');
like($r, qr/200/, '/get_prop responds 200');
like($r, qr/false,true/, '/get_prop: local=false, global=true (nginx default)');
