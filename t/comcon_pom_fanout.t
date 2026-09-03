#!/usr/bin/perl

# COMCON increment D4b — class-F multi-worker fan-out. comcon.bindShared(key,
# quotation, contract, onRequest) is the multi-worker spelling of bindAt: the
# current {epoch, source} lives in nginx.shared (lock-free, visible to every
# worker) and each worker's handler RECONCILES lazily — on each request it reads
# the shared epoch and, if newer than its locally compiled one, recompiles the
# shared source in ITS OWN compartment and swaps (rebuild-on-write per worker).
# So a replace() executed in ONE worker fans out to ALL of them coherently: no
# worker serves a torn state. Only the source string crosses (never a JSValue).

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
worker_processes 4;

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
var locs = nginx.http.servers[0].locations;

function onReq(req, callable, epoch) {
    if (callable === null) {
        req.respond(410, {'content-type':'text/plain',
                          'x-worker': String(nginx.workerIdx)}, 'gone');
        return;
    }
    var v = callable(0);
    req.respond(200, {'content-type':'text/plain',
                      'x-epoch': String(epoch),
                      'x-worker': String(nginx.workerIdx)}, String(v));
}

var h = comcon.bindShared("m",
                          comcon.quote("function(){ return 'v1'; }"),
                          { imports: [] }, onReq);

locs.find(function(l){ return l.path === "/m"; }).handler = h.handler;

locs.find(function(l){ return l.path === "/ctl"; }).handler = function(req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === "replace")      r.epoch = h.replace(comcon.quote("function(){ return 'v2'; }"));
        else if (op === "remove")  r.epoch = h.remove();
        else if (op === "revive")  r.epoch = h.revive();
        else                        r.epoch = h.epoch();
    } catch (e) { r.error = e.message; }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(6);

# helper: hit /m N times, return (joined bodies, joined epochs, #distinct workers)
sub sweep {
    my $n = shift;
    my (%workers, @epochs, @bodies);
    for (1 .. $n) {
        my $r = http_get('/m');
        push @bodies, ($r =~ /\r\n\r\n(v\d|gone)/ ? $1 : '?');
        push @epochs, ($r =~ /x-epoch: (\d+)/ ? $1 : ($r =~ /410/ ? 'X' : '?'));
        $workers{$1}++ if $r =~ /x-worker: (\d+)/;
    }
    return (join(',', @bodies), join(',', @epochs), scalar keys %workers);
}

# phase 1: epoch 0 serves v1 across the pool
my ($b1, $e1, $w1) = sweep(24);
cmp_ok($w1, '>=', 2, "requests land on multiple workers ($w1 distinct)");
unlike($b1, qr/v2/, 'before replace: no worker serves v2');
like($b1, qr/^v1(,v1)*$/, 'before replace: every worker serves v1 (epoch 0)');

# one replace, executed in whichever worker handles /ctl
like(http_get('/ctl?op=replace'), qr/"epoch":1/, 'replace bumps the shared epoch to 1');

# phase 2: EVERY worker now serves v2 — coherent class-F fan-out
my ($b2, $e2, $w2) = sweep(24);
like($b2, qr/^v2(,v2)*$/, 'after replace: every worker serves v2 (fan-out)');
like($e2, qr/^1(,1)*$/,   'after replace: every worker reports epoch 1');
