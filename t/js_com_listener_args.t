#!/usr/bin/perl

# Listener and stream-listener METHODS — hostile argument fuzz.
#
# The last untested COM surface, and the one where structural mutation of live
# routing happens: addServer, addVirtualServer, serverByName, on, addL4Filter,
# addL4SendFilter on the HTTP listener, and the three on the stream listener.
# The setter fuzz (t/js_com_setter_fuzz.t) covered COM *properties*; nothing had
# ever passed these *methods* an argument they did not expect.
#
# This is the surface where the severe defects of this session lived — the
# socket handle aliasing was in this file pair, and the project's own history
# records a `Track L` routing bug from an uninitialised tombstone in addServer.
#
# The properties asserted are deliberately modest, because for a structural
# mutator the interesting question is not "what does it return" but "is the
# server still correct afterwards":
#
#   LIVENESS   no argument crashes or hangs the worker, at config phase or at
#              request time.
#   REFUSAL    every rejection is a proper Error with a message, never a bare
#              throw and never a silent success that mutates nothing.
#   INTACT     after the whole battery, the listener still routes: its real
#              server still answers on its real port, and serverByName still
#              resolves the name it did before.
#
# INTACT is the one that matters.  A method that accepts a bad argument and
# half-applies it would pass LIVENESS and REFUSAL and still have broken the
# routing table, which is exactly the shape of the Track L bug.

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
        server_name  main.local;
        location /r { }
    }
}

stream {
    upstream sback { server 127.0.0.1:8093; }
    server {
        listen 127.0.0.1:8082;
        proxy_pass sback;
    }
}
EOF

$t->write_file('host.js', <<'JS');
var OUT = { refused: [], accepted: [], notError: [], threw: null,
            perMethod: {} };

/* The battery every method is put through.  Rebuilt per call: one entry throws
 * on conversion and must be fresh each time. */
function battery() {
    return [
        ['none',      []],
        ['undefined', [undefined]],
        ['null',      [null]],
        ['true',      [true]],
        ['zero',      [0]],
        ['negative',  [-1]],
        ['nan',       [NaN]],
        ['emptystr',  ['']],
        ['string',    ['not-a-server']],
        ['bigstr',    [new Array(8193).join('x')]],
        ['nulstr',    ['a' + String.fromCharCode(0) + 'b']],
        ['object',    [{}]],
        ['array',     [[]]],
        ['func',      [function () { return 1; }]],
        ['throwing',  [{ toString: function () { throw new Error('boom'); },
                         valueOf:  function () { throw new Error('boom'); } }]],
        ['twoargs',   [{}, {}]],
        ['manyargs',  [1, 2, 3, 4, 5]]
    ];
}

/* Call obj[name] with args; record whether it refused, and whether the refusal
 * was a proper Error.  A method that ACCEPTS a hostile argument is recorded
 * too -- that is not automatically wrong (serverByName legitimately returns
 * null for a name it cannot find), but it must be visible. */
function hammer(obj, name, label) {
    var b = battery(), i;
    OUT.perMethod[label + '.' + name] = 0;
    for (i = 0; i < b.length; i++) {
        /* the method name belongs in the tag: the first version keyed only on
         * the surface, so `http.func` appeared twice for two different methods
         * and an acceptance could not be attributed to the method that made it */
        var tag = label + '.' + name + '.' + b[i][0];
        try {
            obj[name].apply(obj, b[i][1]);
            OUT.accepted.push(tag);
            OUT.perMethod[label + '.' + name]++;
        } catch (e) {
            if (!(e instanceof Error) || !e.message) {
                OUT.notError.push(tag);
            } else {
                OUT.refused.push(tag);
            }
        }
    }
}

var LISTENER = null;
var SRV = null;

try {
    SRV = nginx.http.servers[0];

    /* a socket + listener of our own, so the battery cannot break the
     * listener the test itself is served on */
    var sock = nginx.createSocket('127.0.0.1:19801');
    LISTENER = nginx.http.attach(sock);
    LISTENER.addServer(SRV);              /* activate: addVirtualServer needs it */

    hammer(LISTENER, 'serverByName',     'http');
    hammer(LISTENER, 'addServer',        'http');
    hammer(LISTENER, 'addVirtualServer', 'http');
    hammer(LISTENER, 'addL4Filter',      'http');
    hammer(LISTENER, 'addL4SendFilter',  'http');

    /* on(event, fn) takes two: hammer the event slot with a valid fn, then
     * the fn slot with a valid event */
    var b = battery(), i;
    for (i = 0; i < b.length; i++) {
        try { LISTENER.on.apply(LISTENER, b[i][1].concat([function () {}]));
              OUT.accepted.push('http.on-evt.' + b[i][0]); }
        catch (e) { (e instanceof Error && e.message)
                        ? OUT.refused.push('http.on-evt.' + b[i][0])
                        : OUT.notError.push('http.on-evt.' + b[i][0]); }
        try { LISTENER.on.apply(LISTENER, ['accept'].concat(b[i][1]));
              OUT.accepted.push('http.on-fn.' + b[i][0]); }
        catch (e2) { (e2 instanceof Error && e2.message)
                        ? OUT.refused.push('http.on-fn.' + b[i][0])
                        : OUT.notError.push('http.on-fn.' + b[i][0]); }
    }

    /* the stream listener, same three structural methods */
    if (nginx.stream && nginx.stream.servers && nginx.stream.servers.length) {
        var ssock = nginx.createSocket('127.0.0.1:19802');
        var slis  = nginx.stream.attach
                    ? nginx.stream.attach(ssock) : null;
        if (slis) {
            try { slis.addServer(nginx.stream.servers[0]); } catch (e) { }
            hammer(slis, 'serverByName',     'stream');
            hammer(slis, 'addServer',        'stream');
            hammer(slis, 'addVirtualServer', 'stream');
            OUT.streamCovered = true;
        } else {
            OUT.streamCovered = false;
        }
    }
} catch (e) {
    OUT.threw = String(e && e.message);
}

/* INTACT: after the battery, does the listener still resolve its own name? */
try {
    var r = LISTENER ? LISTENER.serverByName('main.local') : null;
    OUT.stillResolves = (r !== null && r !== undefined);
    OUT.listenerAddr = LISTENER ? String(LISTENER.address) : null;
} catch (e) { OUT.stillResolves = 'threw: ' + String(e.message); }

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path !== '/r') { continue; }
    locs[li].handler = function (req) {
        /* repeat the battery at REQUEST time too: several of these methods
         * behave differently post-fork and that path is separate code */
        var RT = { refused: 0, accepted: 0, notError: 0 };
        try {
            var b = battery(), i;
            for (i = 0; i < b.length; i++) {
                try { LISTENER.serverByName.apply(LISTENER, b[i][1]);
                      RT.accepted++; }
                catch (e) { (e instanceof Error && e.message)
                                ? RT.refused++ : RT.notError++; }
                try { LISTENER.addVirtualServer.apply(LISTENER, b[i][1]);
                      RT.accepted++; }
                catch (e2) { (e2 instanceof Error && e2.message)
                                ? RT.refused++ : RT.notError++; }
            }
        } catch (e) { RT.driverError = String(e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify({ cfg: OUT, runtime: RT }));
    };
}
JS

$t->try_run('no js module')->plan(17);

my $r = http_get('/r');
my ($body) = $r =~ /\r\n\r\n(.*)/s;
my $j = $body ? eval { decode_json($body) } : undef;

ok($j, 'the worker survived the whole battery and answered')
    or diag substr($body // 'no body', 0, 300);

SKIP: {
    skip 'no response', 16 unless $j;

    my $c  = $j->{cfg} || {};
    my $rt = $j->{runtime} || {};

    diag sprintf('config phase: %d refused, %d accepted, %d not-an-Error',
                 scalar @{ $c->{refused} || [] },
                 scalar @{ $c->{accepted} || [] },
                 scalar @{ $c->{notError} || [] });
    diag sprintf('request time: %d refused, %d accepted, %d not-an-Error',
                 $rt->{refused} || 0, $rt->{accepted} || 0,
                 $rt->{notError} || 0);
    diag 'accepted at config phase: ' . join(', ', @{ $c->{accepted} || [] })
        if @{ $c->{accepted} || [] };

    ok(!$c->{threw}, 'the config-phase battery ran to completion')
        or diag 'threw: ' . $c->{threw};
    ok(!$rt->{driverError}, 'the request-time battery ran to completion')
        or diag 'driverError: ' . $rt->{driverError};

    # work verification: 17 inputs x 5 methods + 17 x 2 for on() = 119 minimum
    my $total = scalar(@{ $c->{refused} || [] })
              + scalar(@{ $c->{accepted} || [] })
              + scalar(@{ $c->{notError} || [] });
    cmp_ok($total, '>=', 100, 'the config-phase battery actually ran')
        or diag 'a small count here means most of it was skipped';
    cmp_ok($rt->{refused} + $rt->{accepted} + $rt->{notError}, '>=', 30,
           'the request-time battery actually ran');

    is(scalar @{ $c->{notError} || [] }, 0,
       'every config-phase refusal is a proper Error with a message')
        or diag '  ' . join("\n  ", @{ $c->{notError} || [] });
    is($rt->{notError} || 0, 0,
       'every request-time refusal is a proper Error with a message');

    ok($c->{streamCovered}, 'the stream listener methods were reached too')
        or diag 'stream attach unavailable — the stream half is untested';

    # Per-method acceptance, pinned.  17 hostile inputs go to each method; the
    # ones it ACCEPTS characterise it, and a change here is a change in what the
    # method will swallow.  serverByName legitimately takes any string and
    # answers null for a name it does not know; the L4 filter registrars
    # legitimately take a function; the two STRUCTURAL mutators must take none
    # of it, which is the assertion with teeth.
    my $pm = $c->{perMethod} || {};
    diag "per-method acceptances: "
         . join(', ', map { "$_=$pm->{$_}" } sort keys %$pm);

    is($pm->{'http.addServer'}, 0,
       'addServer accepts nothing from the hostile battery');
    is($pm->{'http.addVirtualServer'}, 0,
       'addVirtualServer accepts nothing from the hostile battery');
    is($pm->{'stream.addServer'}, 0,
       'stream addServer accepts nothing from the hostile battery');
    is($pm->{'stream.addVirtualServer'}, 0,
       'stream addVirtualServer accepts nothing from the hostile battery');
    is($pm->{'http.serverByName'}, 4,
       'serverByName takes the four string inputs and answers null');
    is($pm->{'http.addL4Filter'}, 1,
       'addL4Filter takes only the function');
    is($pm->{'http.addL4SendFilter'}, 1,
       'addL4SendFilter takes only the function');

    # INTACT — the assertion that matters for a structural mutator
    is($c->{stillResolves}, JSON::PP::true,
       'the listener still resolves its own server name afterwards')
        or diag 'stillResolves = ' . ($c->{stillResolves} // 'undef');
    like($c->{listenerAddr} // '', qr/^127\.0\.0\.1:19801$/,
         'and still reports its own address');
}
