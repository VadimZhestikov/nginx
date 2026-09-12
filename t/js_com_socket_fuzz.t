#!/usr/bin/perl

# The socket surface — property fuzz of nginx.createSocket() and NginxSocket.
#
# AUDIT_M-SES.md §3 listed the socket surface as an input surface with no fuzz
# corpus.  It is the sharpest one in src/js: a hand-written address parser fills
# a fixed 48-byte buffer from an arbitrary JS string and the result goes to
# socket()/bind()/listen(), while NginxSocket hands JS a HANDLE into a global
# registry of live file descriptors.  Handles into a reusable table are where
# lifetime bugs live.
#
# Properties asserted:
#
#   LIVENESS      no address, however malformed, crashes or hangs the worker.
#   REFUSAL       every rejection is a proper Error carrying a message; the
#                 parser never accepts an address it cannot faithfully bind.
#   FAITHFUL      an ACCEPTED address binds the address that was asked for --
#                 `sock.address` must read back as the string supplied.  An
#                 embedded NUL used to end the C string early while the JS
#                 string carried on, so the value reviewed and the value bound
#                 were different values.
#   NO ALIASING   a CLOSED socket handle must stay closed.  close() frees its
#                 registry slot for reuse while the JS object keeps its index,
#                 so without a generation the next createSocket() handed the
#                 slot back and every stale handle silently became a live
#                 handle to an unrelated socket -- reading its address and fd,
#                 and closing its listening socket.
#   NO FD LEAK    hundreds of rejected and accepted-then-closed createSocket()
#                 calls must leave the worker's open descriptor count flat.
#                 Every error path after socket() has to close the fd it made,
#                 and nothing else in this suite would ever notice if one did
#                 not: the test would pass and the server would bleed.
#
# The instrument is validated first (/ctl): the fd counter is shown to MOVE when
# descriptors are deliberately held, and the aliasing probe is shown to be
# meaningful by checking the pre-condition it depends on (that a fresh socket
# really does reuse the freed slot).

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $ITER = 1500;      # generated addresses
my $CHURN = 300;      # createSocket calls per fd-leak round

my $t = Test::Nginx->new()->has(qw/http/);

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
        location /sock { }
        location /ctl  { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var SEED = 0x50c6ed01;
var PORT_BASE = 19300;      /* generated binds stay in a private range */

function mix32(x) {
    x = x >>> 0;
    x = (x ^ (x >>> 16)) >>> 0;
    x = Math.imul(x, 0x7feb352d) >>> 0;
    x = (x ^ (x >>> 15)) >>> 0;
    x = Math.imul(x, 0x846ca68b) >>> 0;
    return (x ^ (x >>> 16)) >>> 0;
}

function mkRng(seed) {
    var s = mix32(seed) || 0x9e3779b9;
    return function () {
        s ^= (s << 13); s = s >>> 0;
        s ^= (s >>> 17);
        s ^= (s << 5);  s = s >>> 0;
        return s;
    };
}

/* ------------------------------------------------------ generator ---
 * Addresses are assembled from host and port fragments rather than mutated
 * from a seed: the grammar is two fields and a colon, so composition covers it
 * far better than byte flips, and every interesting case here is a field that
 * is individually plausible.
 */
var HOSTS = [
    '127.0.0.1', '0.0.0.0', '255.255.255.255', '10.0.0.1',
    '', '.', '...', '1', '1.2', '1.2.3', '1.2.3.4.5',
    '256.1.1.1', '-1.0.0.1', '1.2.3.-4', '01.02.03.04',
    '0x7f.0.0.1', '127.0.0.01', 'localhost', 'example.com',
    '[::1]', '::1', '[::ffff:127.0.0.1]',
    ' 127.0.0.1', '127.0.0.1 ', '127.0.0.1\t',
    new Array(46).join('a'), new Array(48).join('b'),
    new Array(49).join('c'), new Array(300).join('d'),
    '127.0.0.1' + String.fromCharCode(0) + '.evil',
    String.fromCharCode(0xe9) + '.0.0.1',
    '😀.0.0.1', '127.0.0.1:', ':127.0.0.1'
];

var PORTS = [
    '0', '1', '80', '65535', '65536', '99999', '-1', '-0', '+80',
    '', ' ', ' 80', '80 ', '08', '0x50', '8e1', '8.0', '80.5',
    '999999999999999999999', 'abc', '80abc', 'abc80',
    String.fromCharCode(0) + '80', '80' + String.fromCharCode(0) + '99',
    '😀',
    /* These three truncate or coerce to a port that is HIGH, and therefore
     * bindable without privilege.  The first corpus used '80' for the same
     * shapes, so every one of them failed at bind() with EACCES and the
     * faithfulness oracle never saw the value it was built to catch: an
     * address that binds something other than what was asked for. */
    '19390' + String.fromCharCode(0) + '99',
    ' 19391',
    '+19392'
];

/* Non-string arguments: the entry point must reject every one of them by
 * type, before any parsing happens. */
function nonStrings() {
    return [
        undefined, null, true, false, 0, 1, NaN, Infinity, -1, 65535,
        {}, [], ['127.0.0.1:80'], function () { return '127.0.0.1:80'; },
        { toString: function () { throw new Error('boom'); } },
        { toString: function () { return '127.0.0.1:19999'; } }
    ];
}

function genAddr(i) {
    var rng = mkRng(SEED ^ mix32(i + 1));
    rng(); rng();
    var h = HOSTS[rng() % HOSTS.length];
    var p = PORTS[rng() % PORTS.length];
    var k = rng() % 16;
    /* A quarter of the corpus is deliberately WELL-FORMED.  Composing only
     * from hostile fragments drove the accept rate to 5 in 1500, which left
     * the faithfulness oracle -- the one that checks a bound address reads
     * back as the address asked for -- with almost nothing to judge.  A fuzzer
     * that never reaches the success path only tests the rejecter. */
    if (k >= 12) {
        return '127.0.0.1:' + (PORT_BASE + (rng() % 200));
    }
    if (k === 0) { return h; }                    /* no colon at all */
    if (k === 1) { return h + ':' + p + ':' + p; }/* two colons */
    if (k === 2) { return ':' + p; }              /* empty host */
    if (k === 3) { return h + ':'; }              /* empty port */
    return h + ':' + p;
}

/* ------------------------------------------------------- the probe --- */
function tryCreate(v) {
    var r = { };
    var s = null;
    try { s = nginx.createSocket(v); }
    catch (e) {
        r.refused = true;
        r.err = String(e && e.message);
        r.isError = (e instanceof Error);
        return r;
    }
    r.refused = false;
    try { r.addr = String(s.address); r.fd = s.fd; r.port = s.port; }
    catch (e2) { r.readErr = String(e2 && e2.message); }
    try { s.close(); r.closed = true; }
    catch (e3) { r.closed = false; r.closeErr = String(e3 && e3.message); }
    return r;
}

function runRange(from, n) {
    var res = { from: from, n: n, tried: 0, accepted: 0, refused: 0,
                badError: 0, unfaithful: 0, notClosed: 0,
                failCount: 0, fails: [] };

    for (var i = from; i < from + n; i++) {
        var v = genAddr(i);
        res.tried++;
        var r = tryCreate(v);

        if (r.refused) {
            res.refused++;
            /* a refusal must be a real Error with something to say */
            if (!r.isError || !r.err || r.err.length === 0) {
                res.badError++;
                if (res.fails.length < 10) {
                    res.fails.push({ i: i, why: 'refusal is not a proper Error',
                                     input: v.slice(0, 60) });
                }
            }
            continue;
        }

        res.accepted++;

        /* FAITHFUL: what was bound must be what was asked for */
        if (r.addr !== v) {
            res.unfaithful++;
            if (res.fails.length < 10) {
                res.fails.push({ i: i, why: 'bound ' + JSON.stringify(r.addr) +
                                            ' for input ' + JSON.stringify(v.slice(0, 60)) });
            }
        }
        if (!r.closed) {
            res.notClosed++;
            if (res.fails.length < 10) {
                res.fails.push({ i: i, why: 'accepted but close() failed: ' +
                                            r.closeErr, input: v.slice(0, 60) });
            }
        }
    }

    res.failCount = res.badError + res.unfaithful + res.notClosed;
    return res;
}

/* Non-string arguments, all of which must be refused by type. */
function nonStringProbe() {
    var vals = nonStrings(), out = { tried: 0, accepted: 0, badError: 0 };
    for (var i = 0; i < vals.length; i++) {
        out.tried++;
        try {
            var s = nginx.createSocket(vals[i]);
            out.accepted++;
            try { s.close(); } catch (ce) { /* nothing to do */ }
        } catch (e) {
            if (!(e instanceof Error)) { out.badError++; }
        }
    }
    return out;
}

/* fd churn: N creates, most refused, the valid ones closed straight away. */
function churn(n) {
    var made = 0, refused = 0;
    for (var i = 0; i < n; i++) {
        var v = (i % 3 === 0)
                ? '127.0.0.1:' + (PORT_BASE + (i % 40))
                : genAddr(1000000 + i);
        try {
            var s = nginx.createSocket(v);
            made++;
            try { s.close(); } catch (ce) { /* reported elsewhere */ }
        } catch (e) { refused++; }
    }
    return { made: made, refused: refused };
}

/* ----------------------------------------------------- the aliasing --- */
function aliasProbe() {
    var o = {};
    var a, b;

    try { a = nginx.createSocket('127.0.0.1:19201'); }
    catch (e) { o.setupError = String(e.message); return o; }

    o.aFd = a.fd;
    o.aAddr = String(a.address);
    a.close();

    /* straight after close the handle must already be dead */
    try { a.fd; o.deadAfterClose = false; }
    catch (e) { o.deadAfterClose = true; }

    try { b = nginx.createSocket('127.0.0.1:19202'); }
    catch (e2) { o.setupError = String(e2.message); return o; }

    o.bFd = b.fd;

    /* PRE-CONDITION for this probe to mean anything: the new socket must
     * actually have been handed the slot the closed one used.  If it were
     * given a different slot, a stale handle would fail to resolve for a
     * reason that has nothing to do with the fix, and this probe would pass
     * while proving nothing. */
    o.slotReused = (o.bFd === o.aFd);

    try { o.staleRead = String(a.address); o.staleReadThrew = false; }
    catch (e3) { o.staleReadThrew = true; }

    try { a.close(); o.staleCloseAccepted = true; }
    catch (e4) { o.staleCloseAccepted = false; }

    /* the live socket must have survived the stale close */
    try { b.fd; o.bSurvived = true; }
    catch (e5) { o.bSurvived = false; }

    try { b.close(); o.bCloseOk = true; }
    catch (e6) { o.bCloseOk = false; }

    return o;
}

/* --------------------------------------------- instrument self-tests --- */
function selfTest() {
    var o = {};

    /* the generator makes distinct, reproducible addresses */
    var seen = {}, i;
    for (i = 0; i < 300; i++) { seen[genAddr(i)] = 1; }
    o.genDistinct = Object.keys(seen).length;
    o.genStable = (genAddr(42) === genAddr(42) && genAddr(42) !== genAddr(43));

    /* a plainly valid address is still accepted -- the control against a
     * "fix" that simply refuses everything */
    var ok = tryCreate('127.0.0.1:19555');
    o.validAccepted = (ok.refused === false && ok.addr === '127.0.0.1:19555'
                       && ok.closed === true);

    /* and a plainly invalid one is still refused */
    var bad = tryCreate('not-an-address');
    o.invalidRefused = (bad.refused === true);

    /* the FAITHFUL oracle can tell a mismatch: compare a known-wrong pair */
    o.faithfulOracleWorks = ('127.0.0.1:1' !== '127.0.0.1:2');

    o.alias = aliasProbe();
    o.nonString = nonStringProbe();
    return o;
}

/* HOLD: deliberately leak descriptors so the Perl-side fd counter can be shown
 * to MOVE.  Without this the "fd count stayed flat" assertion is unfalsifiable
 * -- a counter that reads the wrong pid reports flat forever. */
var HELD = [];
function hold(n) {
    var made = 0;
    for (var i = 0; i < n; i++) {
        try { HELD.push(nginx.createSocket('127.0.0.1:' + (PORT_BASE + 100 + i)));
              made++; }
        catch (e) { /* out of range or in use */ }
    }
    return made;
}
function release() {
    var freed = 0;
    for (var i = 0; i < HELD.length; i++) {
        try { HELD[i].close(); freed++; } catch (e) { /* already gone */ }
    }
    HELD = [];
    return freed;
}

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path === "/sock") {
        locs[li].handler = function (req) {
            var q = req.queryParams, r;
            try {
                if (q.op === 'churn')        { r = churn(parseInt(q.n) || 50); }
                else if (q.op === 'hold')    { r = { held: hold(parseInt(q.n) || 10) }; }
                else if (q.op === 'release') { r = { freed: release() }; }
                else if (q.op === 'nonstring') { r = nonStringProbe(); }
                else { r = runRange(parseInt(q.from) || 0, parseInt(q.n) || 100); }
            } catch (e) { r = { driverError: String(e && e.message) }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(r));
        };
    }
    if (locs[li].path === "/ctl") {
        locs[li].handler = function (req) {
            var o;
            try { o = selfTest(); }
            catch (e) { o = { selfTestError: String(e && e.message) }; }
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify(o));
        };
    }
}
JS

$t->try_run('no js module')->plan(19);

sub jget {
    my ($path) = @_;
    my $r = http_get($path);
    return { httpError => 'no response' } unless defined $r;
    my ($body) = $r =~ /\r\n\r\n(.*)/s;
    return { httpError => 'no body' } unless defined $body && length $body;
    my $j = eval { decode_json($body) };
    return { httpError => 'bad json: ' . substr($body, 0, 200) } unless $j;
    return $j;
}

# --- worker fd counting -----------------------------------------------------
# The handler runs in a worker, and in a worker createSocket() goes through the
# master's manager thread and back over SCM_RIGHTS, so the descriptor lands in
# the worker.  Count there, not in the master.
# NOTE: the master pid is passed IN, never read from $t in here.  A named sub
# that mentions $t closes over it, which keeps the Test::Nginx object alive
# until global destruction -- its teardown checks then run too late to count,
# and the file dies with a bad plan and an unrelated Test2 error.
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
        # ppid is field 4, after comm which may contain spaces inside parens
        if ($line =~ /\)\s+\S+\s+(\d+)/ && $1 == $master) { push @kids, $pid; }
    }
    closedir $d;
    return @kids;
}

sub fd_count {
    my ($pid) = @_;
    opendir(my $d, "/proc/$pid/fd") or return -1;
    my $n = grep { $_ ne '.' && $_ ne '..' } readdir $d;
    closedir $d;
    return $n;
}

sub worker_fds {
    my ($master) = @_;
    my @w = worker_pids($master);
    return (-1, 0) unless @w;
    my $total = 0;
    for my $p (@w) { my $c = fd_count($p); $total += $c if $c >= 0; }
    return ($total, scalar @w);
}

# ---------------------------------------------------------------------------
# 1. The instrument.
# ---------------------------------------------------------------------------
my $c = jget('/ctl');

ok(!$c->{httpError} && !$c->{selfTestError}, 'self-test ran')
    or diag explain $c;
cmp_ok($c->{genDistinct} || 0, '>', 100, 'generator produces distinct addresses');
ok($c->{genStable},        'generator is deterministic and index-addressable');
ok($c->{validAccepted},    'a plainly valid address is still accepted and closes');
ok($c->{invalidRefused},   'a plainly invalid address is still refused');

my $al = $c->{alias} || {};
ok($al->{deadAfterClose}, 'a socket handle is dead immediately after close()');
ok($al->{slotReused},
   'pre-condition: the next socket really does reuse the freed registry slot')
    or diag 'without slot reuse the aliasing probe below proves nothing';
ok($al->{staleReadThrew},
   'a closed handle cannot read the socket that reused its slot');
ok(!$al->{staleCloseAccepted},
   'a closed handle cannot close the socket that reused its slot');
ok($al->{bSurvived},  'the live socket survives a close() through a stale handle');
ok($al->{bCloseOk},   'and its owner can still close it');

my $ns = $c->{nonString} || {};
is($ns->{accepted} || 0, 0, 'every non-string argument is refused');
is($ns->{badError} || 0, 0, 'every non-string refusal is a proper Error');
cmp_ok($ns->{tried} || 0, '>', 10, 'the non-string battery actually ran');

# ---------------------------------------------------------------------------
# 2. Address fuzz.
# ---------------------------------------------------------------------------
my %a;
my @fails;
for (my $from = 0; $from < $ITER; $from += 250) {
    my $r = jget("/sock?from=$from&n=250");
    if ($r->{httpError} || $r->{driverError}) {
        diag "batch at $from failed: " . ($r->{httpError} || $r->{driverError});
        last;
    }
    for my $k (qw/tried accepted refused badError unfaithful notClosed failCount/) {
        $a{$k} = ($a{$k} || 0) + ($r->{$k} || 0);
    }
    push @fails, @{ $r->{fails} || [] };
}

diag sprintf("addresses: %d tried | accepted %d | refused %d",
             $a{tried} || 0, $a{accepted} || 0, $a{refused} || 0);
for my $f (@fails) { diag "  input $f->{i}: $f->{why}"; }

is($a{tried} || 0, $ITER, "generated and tried all $ITER addresses");
cmp_ok($a{accepted} || 0, '>', 100,
       'the accept path was exercised enough for the faithfulness oracle to mean something');
is($a{failCount} || 0, 0,
   'no address was bound unfaithfully, refused improperly, or left open');

# ---------------------------------------------------------------------------
# 3. fd leak, with the counter shown to move first.
# ---------------------------------------------------------------------------
my $master_pid = $t->read_file('nginx.pid');
$master_pid = defined $master_pid ? do { (my $x = $master_pid) =~ s/\s+//g; $x } : '';

my ($fd0, $nw) = worker_fds($master_pid);
my $held = jget('/sock?op=hold&n=12');
my ($fd1) = worker_fds($master_pid);
jget('/sock?op=release');
my ($fd2) = worker_fds($master_pid);

diag sprintf("worker fds: %d proc(s) | baseline %d | holding %d (+%d) | released %d",
             $nw, $fd0, $fd1, $fd1 - $fd0, $fd2);

SKIP: {
    skip 'no worker pid found in /proc', 2 if $fd0 < 0 || $nw == 0;

    cmp_ok($fd1 - $fd0, '>=', 5,
           'the fd counter MOVES when descriptors are deliberately held')
        or diag 'the leak assertion below would be unfalsifiable';

    my ($base) = worker_fds($master_pid);
    jget("/sock?op=churn&n=$CHURN");
    jget("/sock?op=churn&n=$CHURN");
    my ($after) = worker_fds($master_pid);
    diag sprintf("after %d createSocket calls: %d fds (delta %d)",
                 2 * $CHURN, $after, $after - $base);
    cmp_ok($after - $base, '<=', 3,
           "worker fd count is flat across " . (2 * $CHURN) . " createSocket calls");
}
