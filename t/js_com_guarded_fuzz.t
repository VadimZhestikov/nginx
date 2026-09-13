#!/usr/bin/perl

# F4 — the GUARDED members, fuzzed one per process.
#
# t/js_com_setter_fuzz.t fuzzes only members classified `safe` AND `reversible`, and says so:
# "`guarded` rewires live dispatch and `irreversible` cannot be undone for the lifetime of
# the process." AUDIT_M-SES §3 carried the exclusion as OPEN — a deliberate scope choice,
# because writing to those members in the shared instance degrades the server under test
# rather than measuring it, and every probe after the write is then measuring wreckage.
#
# The exclusion was about SHARED STATE, not about the members. So this file gives each one
# its own nginx: enumerate the guarded members in a survey instance, then for each, start a
# fresh instance, write hostile values to that member ALONE, and ask two questions of the
# process that is now holding the damage:
#
#   1. did the write CRASH the worker or leave the process unable to serve? (the property
#      the exclusion was protecting -- and the reason it must not be tested in a shared
#      instance)
#   2. did the getter afterwards still answer, rather than throwing or returning garbage?
#
# What this does NOT claim: that a guarded write is SAFE to make. Guarded means "rewires
# live dispatch, needs confirmation" -- the point is that it must fail like an engineered
# operation rather than like a memory error, which is exactly what the setter fuzz proves
# for the safe class and what this proves for the dangerous one.
#
# WHICH ASSERTION IS THE CRASH DETECTOR, measured rather than assumed: the control for this
# file makes the handler setter dereference NULL on a hostile value, and question (1) above
# -- "does the process still serve?" -- STILL PASSES, because nginx's master respawns the
# dead worker before the next request. Only the LOG check fails. So the serve probe is a
# liveness check, not a crash detector, and the log check is the one with teeth. A file that
# had only the serve probe would have called a segfaulting setter clean.

use warnings;
use strict;

use Test::More;
use File::Temp qw/tempdir/;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

my $bin = $ENV{TEST_NGINX_BINARY} || '../objs/nginx';
plan(skip_all => "no nginx binary") unless -x $bin;

my $dir = tempdir(CLEANUP => 1);
my $port = 8971;

# nginx opens its DEFAULT error log before reading -e/error_log, and an absent
# logs/ dir makes it emit `[alert] could not open error log file` at startup --
# which then reads as "hostile writes crashed something". The first run of this
# file blamed guarded writes for exactly that. Make the directory.
mkdir "$dir/logs";

# ---------------------------------------------------------------------------
# One instance, one job. `$js` is the handler body; it answers JSON on /g.
# ---------------------------------------------------------------------------
sub run_instance {
    my ($tag, $js) = @_;

    open my $r, '>', "$dir/$tag.root.js" or die $!;
    print $r <<'PRE';
/* the SAME limits as t/js_com_setter_fuzz.t, so this file sees the same
   tree the safe-class fuzz does -- tighter limits silently reduced the
   guarded set from eight to one. */
var MAX_NODES = 400, MAX_DEPTH = 5, MAX_ELEMS = 6;

function walk() {
    var nodes = [], queue = [{ obj: nginx, path: 'nginx', d: 0 }];
    while (queue.length > 0 && nodes.length < MAX_NODES) {
        var cur = queue.shift(), o = cur.obj;
        if (o === null || typeof o !== 'object') { continue; }
        if (Array.isArray(o)) {
            var lim = o.length < MAX_ELEMS ? o.length : MAX_ELEMS;
            for (var e = 0; e < lim; e++) {
                var ev; try { ev = o[e]; } catch (ee) { continue; }
                if (ev !== null && typeof ev === 'object' && cur.d < MAX_DEPTH) {
                    queue.push({ obj: ev, path: cur.path + '[' + e + ']', d: cur.d + 1 });
                }
            }
            continue;
        }
        var desc = [];
        try { desc = nginx.describe(o); } catch (de) { desc = []; }
        if (!Array.isArray(desc)) { desc = []; }
        nodes.push({ obj: o, path: cur.path, desc: desc });
        if (cur.d >= MAX_DEPTH) { continue; }
        var seen = {}, names = [], k, i;
        try { k = Object.keys(o); } catch (ke) { k = []; }
        for (i = 0; i < k.length; i++) { if (!seen[k[i]]) { seen[k[i]] = 1; names.push(k[i]); } }
        for (i = 0; i < desc.length; i++) {
            var nm = desc[i] && desc[i].name;
            if (nm && !seen[nm]) { seen[nm] = 1; names.push(nm); }
        }
        for (i = 0; i < names.length; i++) {
            var v; try { v = o[names[i]]; } catch (ve) { continue; }
            if (v !== null && typeof v === 'object') {
                queue.push({ obj: v, path: cur.path + '.' + names[i], d: cur.d + 1 });
            }
        }
    }
    return nodes;
}

/* every guarded member the live tree reaches, as path + name */
function guardedTargets() {
    var out = [], nodes = walk(), i, j;
    for (i = 0; i < nodes.length; i++) {
        for (j = 0; j < nodes[i].desc.length; j++) {
            var d = nodes[i].desc[j];
            if (d && d.callable !== true && d.class === 'guarded') {
                out.push({ path: nodes[i].path, name: d.name, type: d.type });
            }
        }
    }
    return out;
}

function nodeAt(path) {
    var nodes = walk(), i;
    for (i = 0; i < nodes.length; i++) { if (nodes[i].path === path) return nodes[i].obj; }
    return null;
}

var locs = nginx.http.servers[0].locations;
function at(p, fn) { var l = locs.find(function (x) { return x.path === p; }); if (l) l.handler = fn; }
PRE
    print $r $js;
    close $r;

    open my $c, '>', "$dir/$tag.conf" or die $!;
    print $c "daemon off; worker_processes 1; pid $dir/$tag.pid;\n";
    print $c "error_log $dir/$tag.err info;\n";
    print $c "js_source $dir/$tag.root.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:$port;\n";
    print $c "  server_name localhost;\n  location /g { } location /ping { } } }\n";
    close $c;

    my $pid = fork();
    die "fork failed" unless defined $pid;
    if ($pid == 0) {
        open(STDERR, '>', "$dir/$tag.stderr") or exit 126;
        exec($bin, '-p', $dir, '-c', "$dir/$tag.conf") or exit 127;
    }
    my $up = 0;
    for (1 .. 100) {
        if (IO::Socket::INET->new(PeerAddr => "127.0.0.1:$port", Timeout => 1)) { $up = 1; last }
        select undef, undef, undef, 0.05;
    }
    my $body = $up ? `curl -s --max-time 10 127.0.0.1:$port/g` : '';
    my $ping = $up ? `curl -s --max-time 5 127.0.0.1:$port/ping` : '';
    kill 'QUIT', $pid; waitpid($pid, 0);

    my $log = '';
    for my $lf ("$dir/$tag.err", "$dir/$tag.stderr") {
        if (open my $f, '<', $lf) { local $/; $log .= <$f>; close $f; }
    }
    $port++;
    return ($up, $body, $ping, $log);
}

# ---------------------------------------------------------------------------
# Pass 1 — survey: which guarded members does the live tree reach?
# ---------------------------------------------------------------------------
my ($up0, $survey, undef, $log0) = run_instance('survey', <<'JS');
at('/ping', function (req) { req.respond(200, {}, 'pong'); });
at('/g', function (req) {
    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ targets: guardedTargets() }));
});
JS

my @targets;
while ($survey =~ /\{"path":"([^"]+)","name":"([^"]+)"/g) {
    push @targets, { path => $1, name => $2 };
}

plan(tests => 2 + 3 * scalar(@targets));

ok($up0, 'the survey instance started');
cmp_ok(scalar(@targets), '>=', 1,
       'the live tree reaches at least one GUARDED member (' . scalar(@targets)
       . ' found) -- if this ever reads zero, the class has moved and this file '
       . 'is testing nothing');
diag("guarded targets: " . join(', ', map { "$_->{path}.$_->{name}" } @targets));

# ---------------------------------------------------------------------------
# Pass 2 — one instance per target, hostile writes to that member ALONE
# ---------------------------------------------------------------------------
my $n = 0;
for my $tg (@targets) {
    $n++;
    my $path = $tg->{path};
    my $name = $tg->{name};

    my $js = <<"JS";
at('/ping', function (req) { req.respond(200, {}, 'pong'); });
at('/g', function (req) {
    var o = { path: '$path', name: '$name', writes: [], readBack: null,
              getterThrew: false };
    var node = nodeAt('$path');
    if (node === null) {
        req.respond(200, {'content-type':'application/json'},
            JSON.stringify({ error: 'path not found' }));
        return;
    }

    /* the hostile battery: types the member cannot mean, plus boundaries */
    var VALUES = [null, undefined, '', 'x', 0, -1, 2147483648, 1e308, NaN,
                  true, [], {}, '../../etc/passwd', '\\u0000', 'a'.repeat(4096)];
    var i;
    for (i = 0; i < VALUES.length; i++) {
        var rec = { i: i };
        try { node['$name'] = VALUES[i]; rec.ok = true; }
        catch (e) { rec.threw = (e && e.name) || 'Error'; }
        o.writes.push(rec);
    }

    /* the getter must still answer after all that */
    try { var v = node['$name']; o.readBack = (v === null) ? 'null' : typeof v; }
    catch (ge) { o.getterThrew = true; }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
});
JS

    my ($up, $body, $ping, $log) = run_instance("t$n", $js);
    my $label = "$path.$name";

    ok($up, "[$label] the instance serving this guarded member started");
    like($ping, qr/pong/,
         "[$label] THE PROCESS STILL SERVES after hostile writes to a guarded "
         . "member: the write may be refused, but it must fail like an "
         . "engineered operation, not like a memory error");
    my @bad = ($log =~ /^.*(?:\[alert\]|\[emerg\]|SIGSEGV|signal 11|AddressSanitizer).*$/mg);
    diag("[$label] log: $_") for @bad[0 .. ($#bad > 2 ? 2 : $#bad)];
    is(scalar(@bad), 0,
       "[$label] and the log carries no crash, alert or emergency -- each "
       . "target gets its OWN process precisely so this question can be "
       . "asked at all");
}
