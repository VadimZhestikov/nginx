#!/usr/bin/perl

# Upstream peer setters — numeric range validation, on BOTH peer classes.
#
# `upstreams[N].peers[M]` is served by two different wrapper classes, and which
# one you get depends on WHEN you ask:
#
#   NginxPeer    at config phase, built from uscf->servers (the parsed `server`
#                directives).  Writes srv->weight, an ngx_uint_t.
#   NginxRRPeer  at request time, built from the shared round-robin peers.
#                Writes p->weight, an ngx_int_t, under the peers wlock.
#
# A gcov run over the whole suite found NginxPeer's getters and setters at 0%
# coverage: every existing test, including the 10 000-iteration weight stress in
# t_stress/com_upstream_weight.t, reaches the surface at request time and so
# only ever exercises the round-robin twin.  An untested twin of a tested path
# is where the two drift apart, and they had: neither validated its input, but
# only NginxPeer wrote the result into an UNSIGNED field, so
#
#     peers[0].weight = -1        (at config phase)
#
# was accepted and stored ~1.8e19, which is then summed into peers->total_weight
# and decides which backend every request goes to.  JS_ToInt32() is a cast, not
# a check.
#
# Both classes now bound their numbers the way nginx bounds the `server`
# directive that sets the same fields: weight >= 1, maxFails/failTimeout/
# maxConns >= 0.  This file exercises BOTH, at both phases, so the twins cannot
# drift again -- and asserts the legal values still work, because a validator
# that refuses everything would satisfy the refusal half on its own.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy upstream_zone/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    # no zone: peers[] is the config-phase NginxPeer view
    upstream plain {
        server 127.0.0.1:8091;
        server 127.0.0.1:8092 weight=2;
    }

    # zoned: peers[] is the round-robin NginxRRPeer view at request time
    upstream zoned {
        zone zoned 64k;
        server 127.0.0.1:8093 weight=3;
    }

    server {
        listen       127.0.0.1:8080;
        location /p { proxy_pass http://zoned; }
        location /q { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
/* The battery each peer number is put through.  `ok` values must be accepted
 * and read back; `bad` values must be REFUSED, not coerced. */
function probe(peer, label) {
    var r = { label: label, refused: [], accepted: [], wrong: [] };

    function mustRefuse(prop, v, tag) {
        var before = peer[prop];
        try {
            peer[prop] = v;
            /* not refused: record what it became */
            r.wrong.push(prop + '=' + tag + ' -> ' + String(peer[prop]));
            try { peer[prop] = before; } catch (e) { /* best effort */ }
        } catch (e) {
            r.refused.push(prop + ':' + tag);
        }
    }

    function mustAccept(prop, v) {
        try {
            peer[prop] = v;
            if (peer[prop] === v) { r.accepted.push(prop + '=' + v); }
            else { r.wrong.push(prop + '=' + v + ' read back ' + String(peer[prop])); }
        } catch (e) {
            r.wrong.push(prop + '=' + v + ' refused: ' + String(e.message).slice(0, 40));
        }
    }

    /* the defect: a negative weight, stored unsigned on the config-phase class */
    mustRefuse('weight', -1, 'neg');
    mustRefuse('weight', 0, 'zero');
    mustRefuse('weight', -2147483648, 'int32min');
    mustRefuse('maxFails', -1, 'neg');
    mustRefuse('maxConns', -1, 'neg');
    mustRefuse('failTimeout', -1, 'neg');

    /* and the values that must still work */
    mustAccept('weight', 1);
    mustAccept('weight', 5);
    mustAccept('maxFails', 0);
    mustAccept('maxFails', 3);
    mustAccept('maxConns', 0);
    mustAccept('maxConns', 10);
    mustAccept('failTimeout', 0);
    mustAccept('failTimeout', 20);

    return r;
}

/* CONFIG PHASE — NginxPeer, the class gcov found at 0% */
var CFG = {};
try {
    var up = null, ups = nginx.http.upstreams, i;
    for (i = 0; i < ups.length; i++) {
        if (String(ups[i].name) === 'plain') { up = ups[i]; }
    }
    CFG.found = (up !== null);
    if (up) {
        var peers = up.peers;
        CFG.nPeers = peers.length;
        CFG.result = probe(peers[0], 'config/NginxPeer');
        /* leave a sane weight behind for the rest of the run */
        try { peers[0].weight = 1; } catch (e) { /* reported above */ }
    }
} catch (e) { CFG.error = String(e && e.message); }

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path !== "/q") { continue; }
    locs[li].handler = function (req) {
        var out = { cfg: CFG };
        /* RUNTIME — NginxRRPeer, the twin the suite already exercised */
        try {
            var ups = nginx.http.upstreams, up = null, i;
            for (i = 0; i < ups.length; i++) {
                if (String(ups[i].name) === 'zoned') { up = ups[i]; }
            }
            out.rrFound = (up !== null);
            if (up) { out.rr = probe(up.peers[0], 'runtime/NginxRRPeer'); }
        } catch (e) { out.rrError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(out));
    };
}
JS

$t->try_run('no js module')->plan(10);

my $r = http_get('/q');
my ($body) = $r =~ /\r\n\r\n(.*)/s;
my $j = $body ? eval { decode_json($body) } : undef;

ok($j, 'probe responded') or diag substr($body // 'no body', 0, 200);

SKIP: {
    skip 'no response', 9 unless $j;

    my $cfg = $j->{cfg} || {};
    my $cr  = $cfg->{result} || {};
    my $rr  = $j->{rr} || {};

    ok($cfg->{found}, 'config-phase view of the non-zoned upstream was reachable');
    cmp_ok($cfg->{nPeers} || 0, '>=', 2, 'it exposed its peers');
    ok($j->{rrFound}, 'runtime view of the zoned upstream was reachable');

    diag "config/NginxPeer refused:  " . join(', ', @{ $cr->{refused} || [] });
    diag "config/NginxPeer accepted: " . join(', ', @{ $cr->{accepted} || [] });
    diag "runtime/NginxRRPeer refused:  " . join(', ', @{ $rr->{refused} || [] });
    diag "runtime/NginxRRPeer accepted: " . join(', ', @{ $rr->{accepted} || [] });

    is(scalar @{ $cr->{wrong} || ['?'] }, 0,
       'config-phase peer: every out-of-range value refused, every legal one kept')
        or diag '  ' . join("\n  ", @{ $cr->{wrong} || [] });
    is(scalar @{ $rr->{wrong} || ['?'] }, 0,
       'runtime peer: every out-of-range value refused, every legal one kept')
        or diag '  ' . join("\n  ", @{ $rr->{wrong} || [] });

    # Work verification: both halves of each battery must actually have run,
    # or "no wrong values" is the report of a probe that did nothing.
    is(scalar @{ $cr->{refused} || [] }, 6,
       'config-phase peer: all six out-of-range writes were attempted and refused');
    is(scalar @{ $cr->{accepted} || [] }, 8,
       'config-phase peer: all eight legal writes were attempted and kept');
    is(scalar @{ $rr->{refused} || [] }, 6,
       'runtime peer: all six out-of-range writes were attempted and refused');
    is(scalar @{ $rr->{accepted} || [] }, 8,
       'runtime peer: all eight legal writes were attempted and kept');
}
