#!/usr/bin/perl

# M5 DECISION EVIDENCE — two measurements, so the commitment question stops
# being a judgement call.
#
# EXPERIMENT 1: is a real policy COMPUTE-BOUND? The M1 gate measured hand-written
# C at 3.44x the interpreted mirror, but AOT-A measured the engine's own JS->C
# compiler at 1.0x -- nothing -- on that same policy shape, and 5.79x on a
# compute-heavy handler. If real policies look like M1's, compiling their JS buys
# nothing and M5's payoff has to come from the typed STUB ABI instead, which is a
# different (and much larger) deliverable than "emit unboxed C".
#
# Measured IN-PROCESS, deliberately. The obvious experiment is a throughput
# benchmark, and on this box that would be measured through WSL2's mirrored-mode
# firewall, which adds a large fixed per-request cost OUTSIDE the thing under
# test and compresses every ratio toward 1.0 -- i.e. it would manufacture the
# very answer being looked for. An in-process A/B has no network in it at all.
#
# THE ARMS: two copies of the SAME source, one jitCompile()d at config time (the
# master, pre-fork -- the only place it works, since the gcc thread does not
# survive fork). Same inputs, same request, same loop.
#
# RUN IT AGAINST objs_jit. On a build without CONFIG_JIT there is no compiled
# tier, the known-positive control cannot fire, and this reports a FAILURE rather
# than a skip -- a measurement that cannot measure should say so, not pass.
# It lives in t/tools/ and is not part of `prove t/`: it is decision evidence,
# re-run when the question comes up, not a gate.
#
# EXPERIMENT 2: how big is the minimum STUB SET? Every host member a policy
# touches needs a typed C entry point before lowering is worth anything. The
# count is extracted from the policy sources with the D5b-2 CST, and compared
# against the whole classified COM surface.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib '../lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /m5 { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* A REALISTIC token payload. The first version of this fixture defaulted to
 * 'a.b.c', so the hash loop ran over ONE character and the only compute-bearing
 * policy in the set barely computed -- which would have turned "these policies
 * are host-call bound" into a claim the fixture manufactured. */
var DEFAULT_JWT = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.'
    + 'eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIy'
    + 'LCJ0ZW5hbnQiOiJhY21lIiwic2NvcGUiOiJyZWFkOmFsbCB3cml0ZTpvd24iLCJleHAiOjE5'
    + 'OTk5OTk5OTl9.dBjftJeZ4CVPmB92K27uhbUJU1p1r_wW1gFWFOEjXk';

/* Three policies people actually ask for, plus M1's, written as host functions
 * over the surfaces a policy really touches. */
var POLICIES = {

  /* M1's shape: a shared counter + a header read + two response headers. */
  count_tag:
    "function (req, out) {"
  + "  var n = nginx.shared.incr('rl:' + (req.headers.host || '-'), 1);"
  + "  var who = req.headers['x-tenant'] || '-';"
  + "  out.a = 'x-count:' + n;"
  + "  out.b = 'x-tenant-seen:' + who;"
  + "  return n;"
  + "}",

  /* rate limit with a window decision: one host call, then arithmetic. */
  ratelimit:
    "function (req, out) {"
  + "  var k = 'rl:' + (req.headers['x-tenant'] || '-');"
  + "  var n = nginx.shared.incr(k, 1);"
  + "  var limit = 100, burst = 20, over = 0;"
  + "  if (n > limit) { over = n - limit; }"
  + "  out.a = (over > burst) ? 'deny' : 'allow';"
  + "  return over;"
  + "}",

  /* a JWT-ish check: string work + a hash. No host call but the header read. */
  jwtish:
    "function (req, out) {"
  + "  var tok = req.headers['authorization'] || DEFAULT_JWT;"
  + "  var p = tok.split('.');"
  + "  if (p.length !== 3) { out.a = 'bad'; return 0; }"
  + "  var h = 5381, i, s = p[1];"
  + "  for (i = 0; i < s.length; i++) { h = ((h * 33) ^ s.charCodeAt(i)) | 0; }"
  + "  var exp = (h & 0xffff);"
  + "  out.a = (exp > 1000) ? 'ok' : 'expired';"
  + "  return h;"
  + "}",

  /* CONTROL, known-positive: the shape AOT-A measured at 5.79x. If this one
   * does not speed up either, the harness is broken, not the policies. */
  compute_control:
    "function (req, out) {"
  + "  var s = 0, i;"
  + "  for (i = 0; i < 4000; i++) { s = (s + i * 3) | 0; }"
  + "  out.a = s;"
  + "  return s;"
  + "}",

  /* a routing decision: glob matching over the path, pure compute. */
  routing:
    "function (req, out) {"
  + "  var u = req.uri || '/', rules = ['/api/', '/static/', '/admin/',"
  + "     '/v1/', '/v2/', '/health'], i, hit = 'default';"
  + "  for (i = 0; i < rules.length; i++) {"
  + "    if (u.substring(0, rules[i].length) === rules[i]) { hit = rules[i]; break; }"
  + "  }"
  + "  out.a = hit;"
  + "  return hit.length;"
  + "}"
};

/* Two copies per policy: compile exactly one of them, at CONFIG time. */
var ARMS = {}, REPORTS = {};
for (var name in POLICIES) {
    if (!Object.prototype.hasOwnProperty.call(POLICIES, name)) { continue; }
    /* THE TWO ARMS MUST NOT SHARE A CACHE KEY. bc_hash folds the source text,
     * and installation is keyed by that hash, so two functions built from
     * IDENTICAL source are one cache entry: compiling either installs native
     * code into both, and the A/B silently becomes compiled-vs-compiled. (The
     * validity check below caught exactly that: the interpreted arm came back
     * "already compiled".) A distinct comment inside each body changes the hash
     * and nothing else. */
    var mkArm = function (tag) {
        var src = POLICIES[name].replace('{', '{ /* arm:' + tag + ' */ ');
        /* new Function() compiles in GLOBAL scope, so the policy cannot see a
         * module-level var in root.js -- the token is inlined instead. */
        src = src.replace('DEFAULT_JWT', JSON.stringify(DEFAULT_JWT));
        return (new Function('return (' + src + ');'))();
    };
    var interp = mkArm('interp'), compiled = mkArm('compiled');
    REPORTS[name] = nginx.jitCompile(compiled);
    ARMS[name] = { interp: interp, compiled: compiled };
}

/* ---- experiment 2: the stub set, read off the sources with the CST -------- */
function hostMembers(src) {
    var v = comcon.cst(src), out = {};
    var roots = { nginx: 1, req: 1, comcon: 1 };
    v.query('type(MemberExpression)').forEach(function (m) {
        var t = v.src.slice(m.range[0], m.range[1]);
        var dot = t.indexOf('.');
        if (dot <= 0) { return; }
        var base = t.slice(0, dot);
        if (!roots[base]) { return; }
        /* keep base.member, dropping any deeper chain or index */
        var rest = t.slice(dot + 1).split(/[.\[(]/)[0];
        if (rest) { out[base + '.' + rest] = 1; }
    });
    return Object.keys(out);
}

locs.find(function (l) { return l.path === "/m5"; }).handler = function (req) {
    var o = { arms: {}, stubs: {}, reports: {} }, name;
    /* Per-policy iteration counts: one N cannot serve a policy doing two host
     * calls and one doing 4000 arithmetic ops -- the same N makes one arm
     * unmeasurably fast and the other take ten seconds. */
    var ITERS = { count_tag: 200000, ratelimit: 200000, jwtish: 100000,
                  compute_control: 3000, routing: 200000 };
    var out = {};

    for (name in ARMS) {
        if (!Object.prototype.hasOwnProperty.call(ARMS, name)) { continue; }
        var a = ARMS[name], i, t0, t1, t2, N = ITERS[name] || 100000;

        /* WHEN does each arm acquire native code? Read-only, so asking does not
         * change the answer. */
        var tier0 = { interp: nginx.jitStatus(a.interp),
                      compiled: nginx.jitStatus(a.compiled) };

        /* warm both arms equally */
        for (i = 0; i < Math.min(N / 10, 5000); i++) {
            a.interp(req, out); a.compiled(req, out);
        }

        var tier1 = { interp: nginx.jitStatus(a.interp),
                      compiled: nginx.jitStatus(a.compiled) };

        t0 = Date.now();
        for (i = 0; i < N; i++) { a.interp(req, out); }
        t1 = Date.now();
        for (i = 0; i < N; i++) { a.compiled(req, out); }
        t2 = Date.now();

        o.arms[name] = {
            interpMs: (t1 - t0), compiledMs: (t2 - t1),
            ratio: Math.round(((t1 - t0) / Math.max(t2 - t1, 1)) * 100) / 100,
            n: N, installed: REPORTS[name].installed,
            beforeWarm: tier0.interp.compiled + '/' + tier0.compiled.compiled,
            afterWarm: tier1.interp.compiled + '/' + tier1.compiled.compiled
        };
        o.stubs[name] = hostMembers(POLICIES[name]);
    }

    /* the union across policies = the minimum stub set for this set */
    var all = {};
    for (name in o.stubs) {
        if (Object.prototype.hasOwnProperty.call(o.stubs, name)) {
            o.stubs[name].forEach(function (m) { all[m] = 1; });
        }
    }
    o.stubUnion = Object.keys(all).sort();
    o.stubCount = o.stubUnion.length;

    /* VALIDITY: the "interpreted" arm must actually be interpreted. Compiling it
     * now says which it was -- attempted/installed means it had no native code
     * until this moment; skipped with nothing attempted would mean it already
     * did, and the whole A/B would have been compiled-vs-compiled. */
    o.armCheck = {};
    for (name in ARMS) {
        if (Object.prototype.hasOwnProperty.call(ARMS, name)) {
            o.armCheck[name] = nginx.jitCompile(ARMS[name].interp);
        }
    }

    /* the classified COM surface, for scale */
    var d = nginx.describe(), total = 0, k2;
    if (d instanceof Array) {
        for (k2 = 0; k2 < d.length; k2++) {
            var row = d[k2];
            total += (row && row.members) ? row.members.length : 1;
        }
    }
    o.surfaceRows = (d instanceof Array) ? d.length : -1;
    o.surfaceMembers = total;

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(3);

my $r = http_get('/m5');
diag($1) if $r =~ /(\{.*\})/;
ok($r =~ /"stubCount"/, 'the M5 evidence run produced a report');

# The harness must be able to SHOW a speed-up, or "no policy benefits" is a
# statement about the harness. compute_control is the shape AOT-A measured at
# 5.79x end-to-end; in-process, with no request overhead around it, it is far
# larger.
my ($ctl) = $r =~ /"compute_control":\{[^}]*"ratio":([\d.]+)/;
cmp_ok($ctl, '>', 5,
       "the known-positive control compiles to ${ctl}x -- the harness can see a "
       . 'speed-up, so a 1.0x elsewhere is a property of the policy');

# And both arms must be on the tiers they claim, or the comparison is not one.
like($r, qr/"beforeWarm":"0\/1"/,
     'the arms are on different tiers: the interpreted one has no native code, '
     . 'the compiled one does (read with nginx.jitStatus, which never compiles)');
