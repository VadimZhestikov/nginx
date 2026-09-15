#!/usr/bin/perl

# WHAT DOES A CONFINED INVOCATION COST — and does it depend on anyone else?
#
# PERFORMANCE.md had no number for this.  M1's "interpreted policy" is host JS,
# not a fragment invoked through comcon, so every per-request cost the doc set
# quotes for the tier tenants actually run was a model.  That is how an O(heap)
# walk on every invocation stayed invisible: JS_ComputeMemoryUsage() was called
# twice per call to read one counter, on a heap SHARED by every fragment.  Fixed
# with an O(1) JS_GetMallocSize(); t/comcon_invoke_heap_independence.t gates the
# ratio.  This file records the absolute numbers.
#
# THIS IS EVIDENCE, NOT A GATE, for the reason the other files in t/tools/ are:
# absolute timings in the suite are flaky, and the useful output is a number.
#
#     TEST_NGINX_BINARY=$(pwd)/objs/nginx prove -v t/tools/confined-invoke-cost.t
#
# Two measurements, both on ONE worker (co-residency is the premise):
#
#   in-process   microseconds per call of a host JS function and of the same
#                trivial fragment invoked confined, idle and with 200,000 objects
#                retained by a DIFFERENT fragment;
#   per request  wrk throughput for stock nginx, a host JS handler, and a host
#                handler that makes one confined invocation -- idle and loaded.
#
# Needs `wrk` on PATH for the second half; skips it otherwise.

use warnings;
use strict;

use Test::More;
use JSON::PP;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib '../lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /stock    { return 200 "ok\n"; }
        location /host     { }
        location /confined { }
        location /micro    { }
        location /load     { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var by = {}, locs = nginx.http.servers[0].locations;
locs.forEach(function (l) { by[l.path] = l; });

var F = comcon.include("function(a){ return 1; }", { imports: [] });
var ballast = null;

/* the authoring tier (v5.106): a parent that only returns, and a parent that
   authors ONE sub-fragment (once, cached) and invokes it per call -- the
   difference is what a nested invocation costs on top of the outer one */
var P0 = comcon.include("function(a){ return 1; }",
                        { imports: [], grants: { author: comcon.author({ subFragments: 1 }) } });
var P1 = comcon.include(
    "(function(){ var sub = null; return function(a){" +
    "  if (!sub) { sub = author.include('function(a){ return 1; }', { imports: [] }); }" +
    "  return sub({}); }; })()",
    { imports: [], grants: { author: comcon.author({ subFragments: 1 }) } });

function usPerCall(fn) {
    var t0 = Date.now(), n = 0, dt;
    do {
        for (var i = 0; i < 500; i++) { fn(); }
        n += 500;
        dt = Date.now() - t0;
    } while (dt < 300 && n < 5000000);
    return (dt * 1000) / n;
}

by['/host'].handler = function (r) { r.respond(200, {}, 'ok\n'); };

by['/confined'].handler = function (r) { F({}); r.respond(200, {}, 'ok\n'); };

by['/micro'].handler = function (r) {
    var plain = function (a) { return 1; };
    var o = {
        hostCall: usPerCall(function () { plain({}); }),
        confined: usPerCall(function () { F({}); }),
        parentOnly: usPerCall(function () { P0({}); }),
        nested: usPerCall(function () { P1({}); }),
        loaded: ballast !== null
    };
    r.respond(200, {}, JSON.stringify(o));
};

by['/load'].handler = function (r) {
    if (ballast === null) {
        ballast = comcon.include(
            "(function(){ var keep = [], i;"
          + " for (i = 0; i < 200000; i++) { keep.push({ i: i }); }"
          + " return function(a){ return keep.length; }; })()", { imports: [] });
    }
    r.respond(200, {}, String(ballast({})));
};
JS

$t->try_run('no js module')->plan(1);

sub body { my $r = http_get(shift); $r =~ s/^.*?\r\n\r\n//s; return $r; }

sub wrk_rps {
    my ($path) = @_;
    my $port = port(8080);
    my $out = `wrk -t1 -c10 -d4s http://127.0.0.1:$port$path 2>&1`;
    return ($out =~ /Requests\/sec:\s+([\d.]+)/) ? $1 : undef;
}

my $have_wrk = system('which wrk >/dev/null 2>&1') == 0;

my $idle = decode_json(body('/micro'));
diag(sprintf('in-process, idle compartment:   host JS call %.3f us   confined invoke %.3f us',
             $idle->{hostCall}, $idle->{confined}));
diag(sprintf('in-process, nested (v5.106):    parent alone %.3f us   parent+sub-fragment %.3f us'
             . '   => one nested invocation ~%.3f us',
             $idle->{parentOnly}, $idle->{nested}, $idle->{nested} - $idle->{parentOnly}));

my %rps;
if ($have_wrk) {
    for my $p ('/stock', '/host', '/confined') {
        $rps{$p} = wrk_rps($p);
    }
}

like(body('/load'), qr/^200000$/, 'a different fragment now retains 200,000 objects');

my $loaded = decode_json(body('/micro'));
diag(sprintf('in-process, 200,000 retained:   host JS call %.3f us   confined invoke %.3f us',
             $loaded->{hostCall}, $loaded->{confined}));

if ($have_wrk) {
    $rps{'/confined (loaded)'} = wrk_rps('/confined');
    my $base = $rps{'/stock'} || 1;
    for my $p ('/stock', '/host', '/confined', '/confined (loaded)') {
        diag(sprintf('per request, 1 worker, wrk -c10 4s:  %-20s %10.0f req/s  %5.1f%% of stock',
                     $p, $rps{$p} // 0, 100 * ($rps{$p} // 0) / $base));
    }
} else {
    diag('wrk not found: per-request half skipped');
}
