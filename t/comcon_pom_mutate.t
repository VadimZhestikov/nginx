#!/usr/bin/perl

# COMCON increment D4a — POM mutation as REBUILD-ON-WRITE. comcon.bindAt(site,
# quotation, contract) installs an admitted quotation at a live binding site and
# returns an epoch handle: replace(q) recompiles a NEW epoch and swaps the site
# (retaining the prior for rollback), rollback() restores it, remove() tombstones
# (class X), revive() restores. The site is an install(callable, epoch) fn wired
# to the existing location.handler setter — no parallel install path (the shell
# fundament). Rollback history is bounded and superseded fragments are freed, so
# repeated replacement does not leak (a stress endpoint asserts a flat heap).

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
worker_processes 1;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /m   { }
        location /ctl { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs   = nginx.http.servers[0].locations;
var target = locs.find(function(l){ return l.path === "/m"; });

// install callback: wire the confined callable to target.handler (or, when the
// epoch is removed, install a 410 tombstone). This is the ONLY install path —
// exactly the existing COM setter.
function site(callable, epoch) {
    if (callable === null) {
        target.handler = function(req) {
            req.respond(410, {'content-type':'text/plain'}, 'gone');
        };
    } else {
        target.handler = function(req) {
            var v = callable(0);   // the confined fragment returns a value
            req.respond(200, {'content-type':'text/plain',
                              'x-epoch': String(epoch)}, String(v));
        };
    }
}

var h = comcon.bindAt(site,
                      comcon.quote("function(){ return 'v1'; }"),
                      { imports: [] });

var ctl = locs.find(function(l){ return l.path === "/ctl"; });
ctl.handler = function(req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === "replace") {
            r.epoch = h.replace(comcon.quote("function(){ return 'v2'; }"));
        } else if (op === "replace3") {
            r.epoch = h.replace(comcon.quote("function(){ return 'v3'; }"));
        } else if (op === "rollback") {
            r.epoch = h.rollback();
        } else if (op === "remove") {
            r.epoch = h.remove();
        } else if (op === "revive") {
            r.epoch = h.revive();
        } else if (op === "describe") {
            r = h.describe();
        } else if (op === "stress") {
            // many replace cycles; bounded history + fragment free => flat heap.
            var n = parseInt(req.queryParams.n) || 500;
            var before = nginx.jsMemUsage();
            for (var i = 0; i < n; i++) {
                h.replace(comcon.quote("function(){ return 'x" + (i % 7) + "'; }"));
            }
            nginx.gc();
            var after = nginx.jsMemUsage();
            r.delta = after.mallocSize - before.mallocSize;
            r.epoch = h.epoch();
        } else {
            r.epoch = h.epoch();
        }
    } catch (e) { r.error = e.message; }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(12);

# initial epoch 0 serves v1
like(http_get('/m'), qr/x-epoch: 0/,  'epoch 0 header');
like(http_get('/m'), qr/\r\n\r\nv1$/,  'epoch 0 serves v1');

# replace -> epoch 1 serves v2
like(http_get('/ctl?op=replace'), qr/"epoch":1/, 'replace bumps to epoch 1');
like(http_get('/m'), qr/\r\n\r\nv2$/,             'epoch 1 serves v2 (live rewrite)');

# rollback -> epoch 0 serves v1 again
like(http_get('/ctl?op=rollback'), qr/"epoch":0/, 'rollback returns to epoch 0');
like(http_get('/m'), qr/\r\n\r\nv1$/,             'rollback restores v1 exactly');

# remove -> tombstone (410); revive -> back to v1
like(http_get('/ctl?op=remove'), qr/"epoch":/, 'remove accepted');
like(http_get('/m'), qr/410/,                  'removed node serves a tombstone');
http_get('/ctl?op=revive');
like(http_get('/m'), qr/\r\n\r\nv1$/,          'revive restores the last-live epoch');

# describe lists the mutation ops with safety classes
like(http_get('/ctl?op=describe'), qr/"name":"replace","op":"rewrite","cls":"F"/,
     'describe: replace is class F');
like(http_get('/ctl?op=describe'), qr/"name":"remove","op":"remove","cls":"X"/,
     'describe: remove is class X (guarded)');

# 500 replace cycles stay flat (bounded history + fragment free)
my $body = http_get('/ctl?op=stress&n=500');
$body =~ /"delta":(-?\d+)/;
my $delta = $1 // 999999;
cmp_ok($delta, '<', 32768, "500 replace cycles flat (delta=${delta} < 32KB)");
