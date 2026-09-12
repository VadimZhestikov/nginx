#!/usr/bin/perl

# sock.broadcast() — the fleet-wide half of the socket surface.
#
# The socket fuzz (t/js_com_socket_fuzz.t) covers createSocket() and the
# handle lifecycle in ONE worker.  broadcast() is the operation with the widest
# consequence on that object: it asks the master's manager thread to hand the
# listening descriptor to every OTHER worker over SCM_RIGHTS, so one call
# creates a descriptor and a registry entry in every process in the fleet.  Two
# failure modes follow from that and from nothing else in the suite:
#
#   FD LEAK       every broadcast delivers a fresh descriptor to each peer.  If
#                 a peer that already holds the slot does not close the one it
#                 just received, repeated broadcasts bleed descriptors fleet
#                 wide.  This asserts the TOTAL across all workers stays flat,
#                 with the counter first shown to move so the assertion is
#                 falsifiable.
#
#   SLOT INSTALL  the receiving worker installs the socket into its registry by
#                 index.  A registry slot is only as good as its generation --
#                 installing without bumping it would leave handles issued for
#                 the slot's previous occupant resolving again, the aliasing bug
#                 of commit cd391a160 returning through a path that commit did
#                 not cover.
#
#                 HONESTY NOTE.  The stale-handle section below PINS that a
#                 closed handle stays closed across a broadcast, but it is not a
#                 regression test with teeth: it passes both with and without
#                 the generation bump on the receive path.  Reaching the bug
#                 needs the arriving socket to land on the exact slot a given
#                 worker parked a closed handle on, and the slot a broadcast
#                 carries is the SENDER's index, which this test cannot pin
#                 down from JS -- peers are confirmed to receive (the probe
#                 counts arrivals per worker) but the collision could not be
#                 forced.  So the fix it accompanies is defence in depth against
#                 a defect established by inspection, not by reproduction, and
#                 this file should not be read as proving otherwise.
#
# Also fuzzes the argument surface: broadcast() takes no arguments, so every
# hostile value must be ignored or refused, never crash.
#
# Needs more than one worker, or broadcast has no recipients and every
# assertion here is vacuous -- the worker count is asserted, not assumed.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $ROUNDS = 40;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 3;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        location /b   { }
        location /ctl { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var PORT_BASE = 19700;
var HELD = [];

/* broadcast() is declared with no parameters, so anything passed must be
 * ignored or refused -- never reach the manager as data. */
function argBattery() {
    return [
        undefined, null, true, false, 0, -1, NaN, Infinity,
        '', 'x', new Array(4097).join('A'),
        {}, [], [1, 2], function () { return 1; },
        { toString: function () { throw new Error('boom'); } },
        { valueOf: function () { throw new Error('boom'); } }
    ];
}

/* One create + broadcast + close cycle.  The socket is closed again straight
 * away so the ORIGINATING worker's descriptor count returns to where it
 * started; anything left over is in the peers, which is what we want to see. */
function cycle(n) {
    var r = { made: 0, bcast: 0, bcastThrew: 0, closed: 0, errs: [] };

    for (var i = 0; i < n; i++) {
        var s = null;
        try { s = nginx.createSocket('127.0.0.1:' + (PORT_BASE + (i % 16))); r.made++; }
        catch (e) {
            if (r.errs.length < 5) { r.errs.push('create: ' + String(e.message).slice(0, 60)); }
            continue;
        }
        try { s.broadcast(); r.bcast++; }
        catch (e2) {
            r.bcastThrew++;
            if (r.errs.length < 5) { r.errs.push('bcast: ' + String(e2.message).slice(0, 60)); }
        }
        try { s.close(); r.closed++; }
        catch (e3) {
            if (r.errs.length < 5) { r.errs.push('close: ' + String(e3.message).slice(0, 60)); }
        }
    }

    r.worker = nginx.workerIdx;
    return r;
}

/* broadcast() with hostile arguments, and on a socket that is already gone. */
function argProbe() {
    var out = { tried: 0, crashedNothing: true, badError: 0, afterClose: null,
                worker: nginx.workerIdx };
    var vals = argBattery();

    for (var i = 0; i < vals.length; i++) {
        var s = null;
        try { s = nginx.createSocket('127.0.0.1:' + (PORT_BASE + 40 + (i % 8))); }
        catch (e) { continue; }
        out.tried++;
        try { s.broadcast(vals[i]); }
        catch (e2) { if (!(e2 instanceof Error)) { out.badError++; } }
        try { s.close(); } catch (e3) { /* reported by the cycle probe */ }
    }

    /* broadcast on a closed socket must refuse, not reach the manager */
    try {
        var d = nginx.createSocket('127.0.0.1:' + (PORT_BASE + 60));
        d.close();
        try { d.broadcast(); out.afterClose = 'ACCEPTED'; }
        catch (e4) { out.afterClose = /closed or invalid/.test(String(e4.message))
                                      ? 'refused' : 'other: ' + String(e4.message).slice(0, 50); }
    } catch (e5) { out.afterClose = 'setup-failed'; }

    return out;
}

/* Deliberately hold descriptors so the fleet-wide counter can be shown to
 * move.  Without this, "the total stayed flat" is unfalsifiable. */
function hold(n) {
    var made = 0;
    for (var i = 0; i < n; i++) {
        try { HELD.push(nginx.createSocket('127.0.0.1:' + (PORT_BASE + 80 + i)));
              made++; }
        catch (e) { /* port busy */ }
    }
    return { held: made, worker: nginx.workerIdx };
}

function release() {
    var freed = 0;
    for (var i = 0; i < HELD.length; i++) {
        try { HELD[i].close(); freed++; } catch (e) { /* already gone */ }
    }
    HELD = [];
    return { freed: freed, worker: nginx.workerIdx };
}

/* ------------------------------------------- stale handle vs broadcast ---
 * Parks a closed handle on every worker, sends a broadcast, and checks that
 * none of them comes back to life.  The arrival count per worker is reported so
 * the check is not silently vacuous -- but see the honesty note in the file
 * header: peers do receive, yet the parked handle and the arriving socket were
 * not observed to land on the same slot, so this passes with or without the
 * generation bump.  It pins the property; it does not prove the fix.
 */
var STALE = null;
var RECEIVED = [];
/* the fleet-delivery callback: counts what actually arrives in this worker */
nginx.onSocket = function (sock) { RECEIVED.push(sock); };

function staleSetup() {
    var out = { worker: nginx.workerIdx };
    try {
        STALE = nginx.createSocket('127.0.0.1:' + (PORT_BASE + 120 +
                                                   nginx.workerIdx));
        out.fd = STALE.fd;
        STALE.close();
        /* dead right now, before any broadcast can refill the slot */
        try { STALE.fd; out.deadAfterClose = false; }
        catch (e) { out.deadAfterClose = true; }
        out.ok = true;
    } catch (e2) { out.ok = false; out.error = String(e2.message).slice(0, 60); }
    return out;
}

function bcastOne() {
    var out = { worker: nginx.workerIdx };
    try {
        var s = nginx.createSocket('127.0.0.1:' + (PORT_BASE + 140));
        out.fd = s.fd;
        s.broadcast();
        out.sent = true;
        s.close();
    } catch (e) { out.sent = false; out.error = String(e.message).slice(0, 60); }
    return out;
}

function staleCheck() {
    var out = { worker: nginx.workerIdx, had: (STALE !== null),
                received: RECEIVED.length };
    if (STALE === null) { return out; }
    try { out.addr = String(STALE.address); out.resolves = true; }
    catch (e) { out.resolves = false; }
    return out;
}

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path === "/b") {
        locs[li].handler = function (req) {
            var q = req.queryParams, r;
            try {
                if (q.op === 'hold')         { r = hold(parseInt(q.n) || 8); }
                else if (q.op === 'release') { r = release(); }
                else if (q.op === 'args')    { r = argProbe(); }
                else if (q.op === 'stale_setup') { r = staleSetup(); }
                else if (q.op === 'bcast_one')   { r = bcastOne(); }
                else if (q.op === 'stale_check') { r = staleCheck(); }
                else                          { r = cycle(parseInt(q.n) || 5); }
            } catch (e) { r = { driverError: String(e && e.message) }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(r));
        };
    }
    if (locs[li].path === "/ctl") {
        locs[li].handler = function (req) {
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify({ worker: nginx.workerIdx,
                                         pid: nginx.workerPid }));
        };
    }
}
JS

$t->try_run('no js module')->plan(16);

sub jget {
    my ($path) = @_;
    my $r = http_get($path);
    return { httpError => 'no response' } unless defined $r;
    my ($body) = $r =~ /\r\n\r\n(.*)/s;
    return { httpError => 'no body' } unless defined $body && length $body;
    my $j = eval { decode_json($body) };
    return { httpError => 'bad json: ' . substr($body, 0, 120) } unless $j;
    return $j;
}

# --- fleet descriptor accounting -------------------------------------------
sub worker_pids {
    my ($master) = @_;
    return () unless defined $master && $master =~ /^\d+$/;
    my @kids;
    opendir(my $d, '/proc') or return ();
    for my $pid (grep { /^\d+$/ } readdir $d) {
        open(my $fh, '<', "/proc/$pid/stat") or next;
        my $line = <$fh>;
        close $fh;
        next unless defined $line;
        if ($line =~ /\)\s+\S+\s+(\d+)/ && $1 == $master) { push @kids, $pid; }
    }
    closedir $d;
    return @kids;
}

sub fd_count {
    my ($pid) = @_;
    opendir(my $d, "/proc/$pid/fd") or return 0;
    my $n = grep { $_ ne '.' && $_ ne '..' } readdir $d;
    closedir $d;
    return $n;
}

# total across every worker: broadcast's whole point is the OTHER processes
sub fleet_fds {
    my ($master) = @_;
    my @w = worker_pids($master);
    my $total = 0;
    $total += fd_count($_) for @w;
    return ($total, scalar @w);
}

my $master_pid = $t->read_file('nginx.pid');
$master_pid = defined $master_pid ? do { (my $x = $master_pid) =~ s/\s+//g; $x } : '';

my ($fd0, $nworkers) = fleet_fds($master_pid);
diag "fleet: $nworkers worker(s), $fd0 descriptors total";

cmp_ok($nworkers, '>=', 2,
       'more than one worker, so broadcast has somewhere to go')
    or diag 'with a single worker every assertion in this file is vacuous';

# --- the counter must be able to move --------------------------------------
# Requests round-robin across workers and the held list is per-worker, so both
# hold and release have to be issued enough times to reach every one of them.
# A single release landed on some other worker, freed nothing, and left the
# held descriptors polluting the baseline of the leak test below -- which still
# passed, on a baseline that was 8 descriptors too high.
my $reach = 4 * $nworkers;

jget('/b?op=hold&n=8') for 1 .. $reach;
my ($fd1) = fleet_fds($master_pid);
jget('/b?op=release') for 1 .. $reach;
my ($fd2) = fleet_fds($master_pid);
diag "fleet fds: baseline $fd0 | holding $fd1 (+@{[ $fd1 - $fd0 ]}) | released $fd2";

cmp_ok($fd1 - $fd0, '>=', 4,
       'the fleet descriptor counter MOVES when descriptors are held')
    or diag 'the leak assertion below would be unfalsifiable';

cmp_ok(abs($fd2 - $fd0), '<=', 2,
       'and comes back down when they are released')
    or diag 'the control leaked, so the leak baseline below is not a baseline';

# --- argument surface -------------------------------------------------------
my $ar = jget('/b?op=args');
ok(!$ar->{httpError} && !$ar->{driverError}, 'argument probe ran')
    or diag explain $ar;
cmp_ok($ar->{tried} || 0, '>', 10, 'the argument battery actually ran');
is($ar->{badError} || 0, 0, 'every broadcast refusal is a proper Error');
is($ar->{afterClose}, 'refused', 'broadcast on a closed socket is refused');

# --- a closed handle must not come back to life when a broadcast arrives ----
# Every worker parks a closed handle on slot 0, then one broadcast is sent to
# the fleet carrying that same slot index.
my %setup;
for (1 .. 30 * $nworkers) {
    my $r = jget('/b?op=stale_setup');
    $setup{ $r->{worker} } = $r if defined $r->{worker};
}
my @ready = grep { $setup{$_}{ok} } keys %setup;
my @dead  = grep { $setup{$_}{deadAfterClose} } @ready;

jget('/b?op=bcast_one') for 1 .. 2;

my %check;
for (1 .. 30 * $nworkers) {
    my $r = jget('/b?op=stale_check');
    $check{ $r->{worker} } = $r if defined $r->{worker} && $r->{had};
}
my @resolved = grep { $check{$_}{resolves} } keys %check;

diag "  worker $_ received " . ($check{$_}{received} // '?') . " broadcast socket(s)"
    for sort keys %check;
diag sprintf("stale handles parked on %d worker(s), checked on %d, resolving after broadcast: %d",
             scalar @ready, scalar keys %check, scalar @resolved);
diag "  worker $_ stale handle now reads: $check{$_}{addr}"
    for grep { $check{$_}{resolves} } keys %check;

cmp_ok(scalar @ready, '>=', 2,
       'a closed handle was parked on more than one worker')
    or diag 'without that the broadcast has no stale handle to collide with';
is(scalar @dead, scalar @ready,
   'each parked handle was dead immediately after its close()');
cmp_ok(scalar keys %check, '>=', 1,
       'at least one parked handle was reachable again after the broadcast')
    or diag 'no worker with a parked handle was reached; the check below is vacuous';
is(scalar @resolved, 0,
   'no closed handle comes back to life across a broadcast (pin, not a proof)');

# --- the leak test ----------------------------------------------------------
# NOT "flat": a peer that receives a broadcast KEEPS the descriptor -- that is
# what broadcast is for -- so the first rounds legitimately add some, until each
# peer's registry is full and further arrivals hit the already-occupied path and
# are closed.  The property is therefore SATURATION, not flatness: run two equal
# phases and require the second to add essentially nothing.  A per-broadcast
# leak would grow both phases alike, and asserting a flat count against a magic
# constant would have hidden exactly that.
my %seen;
my ($cycles, $bcast, $threw) = (0, 0, 0);

sub phase {
    my ($rounds) = @_;
    my ($c, $b, $x) = (0, 0, 0);
    for my $i (1 .. $rounds) {
        my $r = jget('/b?n=5');
        next if $r->{httpError} || $r->{driverError};
        $seen{ $r->{worker} } = 1 if defined $r->{worker};
        $c += $r->{made}  || 0;
        $b += $r->{bcast} || 0;
        $x += $r->{bcastThrew} || 0;
    }
    $cycles += $c; $bcast += $b; $threw += $x;
    return $b;
}

my ($base) = fleet_fds($master_pid);
my $b1 = phase($ROUNDS);
my ($mid) = fleet_fds($master_pid);
my $b2 = phase($ROUNDS);
my ($after) = fleet_fds($master_pid);

diag sprintf("%d sockets created, %d broadcast (%d refused), across %d worker(s)",
             $cycles, $bcast, $threw, scalar keys %seen);
diag sprintf("fleet fds: %d -> %d (phase 1, %d broadcasts, +%d) -> %d " .
             "(phase 2, %d broadcasts, +%d)",
             $base, $mid, $b1, $mid - $base, $after, $b2, $after - $mid);

cmp_ok($cycles, '>', 100, 'a real number of create/broadcast/close cycles ran');
cmp_ok($bcast, '>', 100, 'broadcast was actually reached, not just attempted');
is($threw, 0, 'no broadcast was refused unexpectedly');
cmp_ok(scalar keys %seen, '>=', 2, 'the cycles ran on more than one worker');
cmp_ok($after - $mid, '<=', 2,
       "descriptors SATURATE: the second $b2 broadcasts add almost nothing")
    or diag 'phase 2 kept growing, so the peers are not reusing their slots';
