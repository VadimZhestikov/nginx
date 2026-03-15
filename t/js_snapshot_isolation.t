#!/usr/bin/perl

# Phase 5 — cross-request snapshot isolation / persistence tests.
#
# These tests verify the three write-mode invariants across HTTP requests:
#
#  WRITE_GLOBAL (default)
#    A value written from a request handler is visible to the NEXT request
#    because only the shared global struct is modified.
#
#  WRITE_LOCAL
#    A value written from a request handler is NOT visible to the next
#    request because only the per-request snapshot is modified.
#
#  WRITE_BOTH
#    Both the per-request snapshot AND the global struct are modified, so
#    the next request also sees the new value.
#
# Sub-object isolation is covered separately by js_snapshot_sub_obj.t.
# This file focuses on NginxLocation (core loc_conf) properties only.
#
# Test layout (one location per scenario to avoid cross-group pollution):
#
#  /global_a  — property: sendfile, initial: off
#    round-1: WRITE_GLOBAL sendfile=true  → reads back "true"
#    round-2: plain read                  → should see "true" (persisted)
#
#  /local_a   — property: sendfile, initial: off
#    round-1: WRITE_LOCAL  sendfile=true  → reads back "true" (local)
#    round-2: plain read                  → should see "false" (isolated)
#    round-3: WRITE_LOCAL  sendfile=true  → round-3 starts fresh; reads "true"
#    round-4: plain read                  → still "false" globally
#
#  /both_a    — property: etag, initial: on (nginx default)
#    round-1: WRITE_BOTH   etag=false     → reads back "false" (local=false)
#    round-2: plain read                  → should see "false" (global updated)
#
#  /same_req  — property: chunkedTransferEncoding, initial: on
#    single request: WRITE_LOCAL chunked=false, then
#      getProperty("local")  → false  (new snapshot value)
#      getProperty("global") → true   (global untouched)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(24);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/isolation_handlers.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        sendfile  off;
        etag      on;

        location /global_a  { }   # WRITE_GLOBAL scenario
        location /local_a   { }   # WRITE_LOCAL  scenario
        location /both_a    { }   # WRITE_BOTH   scenario
        location /same_req  { }   # same-request read-mode check
        location /plain_g   { }   # plain read for /global_a value
        location /plain_l   { }   # plain read for /local_a value
        location /plain_b   { }   # plain read for /both_a value
    }
}
EOF

$t->write_file('isolation_handlers.js', <<'JS');
(function() {
    const srv  = nginx.http.servers[0];
    const locs = srv.locations;

    function set(path, fn) {
        const l = locs.find(l => l.path === path);
        if (l) { l.handler = fn; }
    }

    // ---- WRITE_GLOBAL scenario (/global_a) ----
    // Writes sendfile=true globally; visible to all subsequent requests.
    set('/global_a', function(r) {
        const loc = r.location;
        loc.setWriteMode("global");
        loc.sendfile = true;
        r.respond(200, {}, String(loc.sendfile));
    });

    // ---- WRITE_LOCAL scenario (/local_a) ----
    // Writes sendfile=true to snapshot only; global stays false.
    set('/local_a', function(r) {
        const loc = r.location;
        loc.setWriteMode("local");
        loc.sendfile = true;
        // read via explicit "local" mode to confirm snapshot value
        const local_v  = loc.getProperty("sendfile", "local");
        const global_v = loc.getProperty("sendfile", "global");
        r.respond(200, {}, local_v + "," + global_v);
    });

    // ---- WRITE_BOTH scenario (/both_a) ----
    // Writes etag=false to both snapshot and global.
    set('/both_a', function(r) {
        const loc = r.location;
        loc.setWriteMode("both");
        loc.etag = false;
        const local_v  = loc.getProperty("etag", "local");
        const global_v = loc.getProperty("etag", "global");
        r.respond(200, {}, local_v + "," + global_v);
    });

    // ---- Plain reads (no mutation) ----
    set('/plain_g', function(r) {
        const loc = locs.find(l => l.path === '/global_a');
        r.respond(200, {}, String(loc.sendfile));
    });

    set('/plain_l', function(r) {
        const loc = locs.find(l => l.path === '/local_a');
        r.respond(200, {}, String(loc.sendfile));
    });

    set('/plain_b', function(r) {
        const loc = locs.find(l => l.path === '/both_a');
        r.respond(200, {}, String(loc.etag));
    });

    // ---- Same-request read-mode check (/same_req) ----
    // WRITE_LOCAL chunked=false; getProperty local=false, global=true.
    set('/same_req', function(r) {
        const loc = r.location;
        loc.setWriteMode("local");
        loc.chunkedTransferEncoding = false;
        const local_v  = loc.getProperty("chunkedTransferEncoding", "local");
        const global_v = loc.getProperty("chunkedTransferEncoding", "global");
        r.respond(200, {}, local_v + "," + global_v);
    });
}());
JS

$t->run();

# -----------------------------------------------------------------------
# WRITE_GLOBAL: change is visible to the next request
# -----------------------------------------------------------------------

my $r;

$r = http_get('/global_a');
like($r, qr/200/, 'WRITE_GLOBAL: mutate request 200');
like($r, qr/true/, 'WRITE_GLOBAL: mutate request reads back true');

$r = http_get('/plain_g');
like($r, qr/200/, 'WRITE_GLOBAL: plain read 200');
like($r, qr/true/, 'WRITE_GLOBAL: plain read sees persisted true');

# -----------------------------------------------------------------------
# WRITE_LOCAL: change is NOT visible to the next request
# -----------------------------------------------------------------------

# Initial plain_l should be false (sendfile off; global unchanged so far)
$r = http_get('/plain_l');
like($r, qr/200/, 'WRITE_LOCAL: initial plain read 200');
like($r, qr/false/, 'WRITE_LOCAL: initial plain read is false');

$r = http_get('/local_a');
like($r, qr/200/, 'WRITE_LOCAL: mutate request 200');
like($r, qr/true,false/, 'WRITE_LOCAL: local=true,global=false in same request');

# Global must still be false after the local-only write
$r = http_get('/plain_l');
like($r, qr/200/, 'WRITE_LOCAL: plain read after mutation 200');
like($r, qr/false/, 'WRITE_LOCAL: global still false after local write');

# A second WRITE_LOCAL request must also start from fresh snapshot
$r = http_get('/local_a');
like($r, qr/200/, 'WRITE_LOCAL round-2: mutate request 200');
like($r, qr/true,false/, 'WRITE_LOCAL round-2: still true,false (fresh snapshot)');

$r = http_get('/plain_l');
like($r, qr/200/, 'WRITE_LOCAL round-2: plain read 200');
like($r, qr/false/, 'WRITE_LOCAL round-2: global still false');

# -----------------------------------------------------------------------
# WRITE_BOTH: change IS visible to the next request (global was updated)
# -----------------------------------------------------------------------

# Initial: etag=on (nginx default); plain_b should be true
$r = http_get('/plain_b');
like($r, qr/200/, 'WRITE_BOTH: initial plain read 200');
like($r, qr/true/, 'WRITE_BOTH: initial etag is true');

$r = http_get('/both_a');
like($r, qr/200/, 'WRITE_BOTH: mutate request 200');
like($r, qr/false,false/, 'WRITE_BOTH: local=false,global=false in same request');

$r = http_get('/plain_b');
like($r, qr/200/, 'WRITE_BOTH: plain read after mutation 200');
like($r, qr/false/, 'WRITE_BOTH: global updated to false');

# -----------------------------------------------------------------------
# Same-request read-mode: WRITE_LOCAL + getProperty("local"/"global")
# -----------------------------------------------------------------------

$r = http_get('/same_req');
like($r, qr/200/, 'READ_LOCAL: same-request check 200');
like($r, qr/false,true/, 'READ_LOCAL: local=false, global=true (nginx default)');

# plain_l is already false, so a second same_req round verifies freshness
$r = http_get('/same_req');
like($r, qr/200/, 'READ_LOCAL round-2: 200');
like($r, qr/false,true/, 'READ_LOCAL round-2: still false,true (global unchanged)');
