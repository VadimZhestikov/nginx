#!/usr/bin/perl

# AOT-A: nginx.jitCompile(fn[, opts]) — opt-in load-time compilation of host JS.
#
# Host JS loaded via js_source is never compiled otherwise: the engine's
# automatic path only ENQUEUES, nothing in src/js drains it, and the gcc worker
# is a pthread that does not survive fork() -- so it is inert in every worker.
# Compilation therefore has to happen pre-fork in the master at init_conf,
# which is where this call runs.
#
# The contract under test is the REPORT, not a boolean. An earlier version
# returned js_comcon_aot_compile() == 0, which is true as soon as the argument
# is a bytecode function; that hid a real codegen bug (a stale extern made gcc
# reject every direct-call function) while still reporting success. So:
# `installed` is the only field that means compiled code exists, and these
# checks pin it.
#
# Runs against BOTH builds. Without -DCONFIG_JIT the call reports all zeros
# rather than pretending, so the behavioural assertions are gated on
# walked > 0 and the shape is asserted unconditionally.

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location / { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    /* A tree: outer + two leaves, so `walked` is meaningfully > 1. */
    function tree() {
        function leaf1(x) { var s = 0; for (var i = 0; i < 40; i++) { s += i * x; } return s; }
        function leaf2(x) { return leaf1(x) + 1; }
        return leaf1(2) + leaf2(3);
    }

    var first  = nginx.jitCompile(tree);
    var second = nginx.jitCompile(tree);          /* idempotent */
    var capped = nginx.jitCompile(function cap() {
        function c1() { return 1; }
        function c2() { return 2; }
        return c1() + c2();
    }, { maxFunctions: 1 });

    var threw = 'no';
    try { nginx.jitCompile(42); } catch (e) { threw = 'yes'; }

    var loc = nginx.http.servers[0].locations.find(function (l) { return l.path === '/'; });
    loc.handler = function (r) {
        r.respond(200, {
            'X-Keys':       Object.keys(first).sort().join(','),
            'X-Walked':     String(first.walked),
            'X-Attempted':  String(first.attempted),
            'X-Installed':  String(first.installed),
            'X-2nd-Att':    String(second.attempted),
            'X-2nd-Skip':   String(second.skipped),
            'X-Cap-Att':    String(capped.attempted),
            'X-Cap-Hit':    String(capped.budgetHit),
            'X-Threw':      threw,
            'X-Sum':        String(tree()),
        }, 'ok');
    };
})();
JS

$t->run()->plan(9);

###############################################################################

sub hdr {
    my ($r, $name) = @_;
    return $r =~ /^\Q$name\E:\s*(.+?)\x0d?$/mi ? $1 : undef;
}

my $r = http_get('/');

is(hdr($r, 'X-Keys'), 'attempted,budgetHit,installed,ms,skipped,walked',
   'jitCompile returns a report, not a boolean');
is(hdr($r, 'X-Threw'), 'yes', 'jitCompile(non-function) throws');

# the function still works after being compiled -- leaf1(2)+leaf2(3) = 1560+2341
is(hdr($r, 'X-Sum'), '3901', 'the compiled function still computes correctly');

# Deliberately NOT asserted unconditionally: without -DCONFIG_JIT this is 0,
# which is the honest answer, not a failure.
my $walked = hdr($r, 'X-Walked');
like($walked, qr/^\d+$/, 'walked is reported as a count');

SKIP: {
    skip 'built without -DCONFIG_JIT (jitCompile reports zeros by design)', 5
        if $walked == 0;

    cmp_ok($walked, '>=', 3, 'the walk descends into nested functions');
    cmp_ok(hdr($r, 'X-Installed'), '>', 0,
           'installed > 0 -- compiled code actually exists (not just "eligible")');
    is(hdr($r, 'X-Installed'), hdr($r, 'X-Attempted'),
       'every function attempted was installed');

    # Second call must do no work: everything already has a jit_func.
    is(hdr($r, 'X-2nd-Att'), '0', 'a second jitCompile attempts nothing');
    cmp_ok(hdr($r, 'X-2nd-Skip'), '>', 0, 'and reports them as skipped');
}
