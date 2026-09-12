#!/usr/bin/perl

# The "converted, not checked" class — the two families a sweep of src/js found
# still unguarded after the peer (5186565a1) and SSL (0ebac7e47) fixes.
#
# JS_ToInt32()/JS_ToInt64() are CASTS, not checks: they answer 0 for NaN, for {}
# and for "abc" without reporting an error, and they hand back a negative that
# the caller then stores in an ngx_uint_t.  Grepping src/js for that exact shape
# -- a converted value cast into an unsigned or time field -- found 94 sites, 81
# of them already guarded.  Of the 13 that were not, four were false positives
# (two already checked with `>= 0`, two reading an internal array length rather
# than anything a caller supplies) and nine were real, in two families:
#
#   RESPONSE STATUS   respond(status) and writeHead(status), 2 sites.  The
#                     widest reach in the whole class: every JS handler calls
#                     one of them.  Unchecked, the status went onto the wire as
#                     the status line, so respond(-1) emitted
#                     "HTTP/1.1 18446744073709551615" and respond({}) emitted
#                     "HTTP/1.1 000" -- malformed responses from a reverse
#                     proxy, which every intermediary downstream then has to
#                     guess about.  Asserted HERE ON THE WIRE, not through the
#                     client library, because the defect IS the bytes.
#
#   STREAM PEERS      7 sites across the two stream peer classes -- the exact
#                     twin of the HTTP peer bug, predicted from it and confirmed
#                     by the sweep.  `stream.upstreams[0].peers[0].weight = -1`
#                     stored ~1.8e19 into a field that
#                     `peers->total_weight += (ngx_uint_t) q->weight` then sums.
#
# All nine now go through one shared check, ngx_js_com_num_range() in
# ngx_js_com.c, and the two previously-local helpers were collapsed onto it.
# Three copies of a check is how the HTTP and stream peer setters came to differ
# in the first place; the fix for that is a shared function, not a shared
# convention.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        location /s { }
        location /p { }
    }
}

stream {
    upstream sback {
        server 127.0.0.1:8091;
        server 127.0.0.1:8092 weight=2;
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* ---- response status: the handler echoes whatever it was asked for ---- */
function statusHandler(req) {
    var v = req.queryParams.v, st;
    if (v === 'neg')       { st = -1; }
    else if (v === 'zero') { st = 0; }
    else if (v === 'big')  { st = 99999; }
    else if (v === 'p31')  { st = 2147483648; }
    else if (v === 'nan')  { st = NaN; }
    else if (v === 'obj')  { st = {}; }
    else if (v === 'str')  { st = 'abc'; }
    else if (v === 'low')  { st = 99; }
    else if (v === 'high') { st = 600; }
    else if (v === 'min')  { st = 100; }
    else if (v === 'max')  { st = 599; }
    else if (v === 'teapot') { st = 418; }
    else                   { st = 200; }

    try {
        req.respond(st, { 'content-type': 'text/plain' }, 'ok');
    } catch (e) {
        /* refused: answer 400 with a marker, so the wire shows a REAL status
         * line either way and the test can tell refusal from acceptance */
        req.respond(400, { 'content-type': 'text/plain' }, 'REFUSED');
    }
}

/* ---- stream peers: both classes ---- */
function peerProbe() {
    var r = { refused: [], accepted: [], wrong: [], found: false };

    function mustRefuse(o, prop, v, tag) {
        var before = o[prop];
        try {
            o[prop] = v;
            r.wrong.push(prop + '=' + tag + ' accepted -> ' + String(o[prop]));
            try { o[prop] = before; } catch (e) { /* best effort */ }
        } catch (e) { r.refused.push(prop + ':' + tag); }
    }
    function mustAccept(o, prop, v) {
        try {
            o[prop] = v;
            if (o[prop] === v) { r.accepted.push(prop + '=' + v); }
            else { r.wrong.push(prop + '=' + v + ' read back ' + String(o[prop])); }
        } catch (e) {
            r.wrong.push(prop + '=' + v + ' refused: ' + String(e.message).slice(0, 40));
        }
    }

    try {
        var ups = nginx.stream.upstreams;
        if (!ups || !ups.length) { return r; }
        var peers = ups[0].peers;
        if (!peers || !peers.length) { return r; }
        r.found = true;
        r.nPeers = peers.length;

        var p = peers[0];
        mustRefuse(p, 'weight', -1, 'neg');
        mustRefuse(p, 'weight', 0, 'zero');
        mustRefuse(p, 'weight', NaN, 'nan');
        mustRefuse(p, 'weight', {}, 'obj');
        mustRefuse(p, 'maxFails', -1, 'neg');
        mustRefuse(p, 'maxConns', -1, 'neg');
        mustRefuse(p, 'failTimeout', -1, 'neg');

        mustAccept(p, 'weight', 1);
        mustAccept(p, 'weight', 3);
        mustAccept(p, 'maxFails', 0);
        mustAccept(p, 'maxConns', 5);
        mustAccept(p, 'failTimeout', 10);

        try { p.weight = 1; p.maxFails = 1; p.maxConns = 0; p.failTimeout = 10; }
        catch (e) { r.restoreFailed = String(e.message).slice(0, 50); }
    } catch (e) { r.error = String(e && e.message); }

    return r;
}

/* config phase: the non-zoned stream peer view */
var CFG = peerProbe();

for (var li = 0; li < locs.length; li++) {
    if (locs[li].path === '/s') { locs[li].handler = statusHandler; }
    if (locs[li].path === '/p') {
        locs[li].handler = function (req) {
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify({ cfg: CFG, rt: peerProbe() }));
        };
    }
}
JS

$t->try_run('no js module')->plan(14);

# ---------------------------------------------------------------------------
# 1. Status codes, read off the wire.
# ---------------------------------------------------------------------------
sub status_line {
    my ($v) = @_;
    my $s = IO::Socket::INET->new(Proto => 'tcp',
                                  PeerAddr => '127.0.0.1:' . port(8080));
    return 'NO SOCKET' unless $s;
    print $s "GET /s?v=$v HTTP/1.0\r\nHost: x\r\n\r\n";
    local $/;
    my $r = <$s>;
    close $s;
    return 'NO RESPONSE' unless defined $r;
    my ($first) = split /\r?\n/, $r;
    return defined $first && length $first ? $first : '(empty)';
}

my %line = map { $_ => status_line($_) }
           qw/ok neg zero big p31 nan obj str low high min max teapot/;
diag "  $_ => $line{$_}" for sort keys %line;

is($line{ok},     'HTTP/1.1 200 OK',  'a normal status is unchanged');
is($line{teapot}, 'HTTP/1.1 418 ',    'an unusual but legal status still works')
    or diag 'over-refusal: legal codes must keep working';
is($line{min},    'HTTP/1.1 100 ',    'the low end of the range is accepted');
is($line{max},    'HTTP/1.1 599 ',    'the high end of the range is accepted');

# every out-of-range value must be REFUSED, and the refusal path answers 400
for my $v (qw/neg zero big p31 nan obj str low high/) {
    is($line{$v}, 'HTTP/1.1 400 Bad Request', "status '$v' is refused");
}

# ---------------------------------------------------------------------------
# 2. Stream peers, both classes.
# ---------------------------------------------------------------------------
my $r = http_get('/p');
my ($body) = $r =~ /\r\n\r\n(.*)/s;
my $j = $body ? eval { decode_json($body) } : undef;

SKIP: {
    skip 'no json', 1 unless $j;
    my $c = $j->{cfg} || {};
    my $rt = $j->{rt} || {};

    diag "stream peers config-phase refused:  " . join(', ', @{ $c->{refused} || [] });
    diag "stream peers config-phase accepted: " . join(', ', @{ $c->{accepted} || [] });
    diag "stream peers runtime     refused:  " . join(', ', @{ $rt->{refused} || [] });

    my @problems;
    push @problems, 'config view not found' unless $c->{found};
    push @problems, 'runtime view not found' unless $rt->{found};
    push @problems, @{ $c->{wrong} || [] };
    push @problems, @{ $rt->{wrong} || [] };
    push @problems, 'config refused ' . scalar(@{ $c->{refused} || [] }) . '/7'
        unless scalar(@{ $c->{refused} || [] }) == 7;
    push @problems, 'config accepted ' . scalar(@{ $c->{accepted} || [] }) . '/5'
        unless scalar(@{ $c->{accepted} || [] }) == 5;
    push @problems, 'runtime refused ' . scalar(@{ $rt->{refused} || [] }) . '/7'
        unless scalar(@{ $rt->{refused} || [] }) == 7;
    push @problems, 'runtime accepted ' . scalar(@{ $rt->{accepted} || [] }) . '/5'
        unless scalar(@{ $rt->{accepted} || [] }) == 5;

    is(scalar @problems, 0,
       'stream peers: both classes bound their numbers, both keep legal values')
        or diag '  ' . join("\n  ", @problems);
}
