#!/usr/bin/perl

# nginx.grantToTenant(name) — the onboarding-delta name registry.
#
# This surface had 0% coverage, which is how it came to lie.  It once published
# a socket into the tenant compartment under `name`; the M-CFG convergence
# removed that compartment and nothing replaced the read, so what was left
# validated a NginxSocket argument, stored its handle, and never looked at it
# again.  A caller was told a capability had been conferred when none had, and
# nginx.tenantLearning() then listed the name among its `grants`.
#
# The drift went unnoticed because the two halves had opposite coverage:
# tenantLearning() (the READER of the name) sits at 85.7% and is exercised by
# t/comcon_include_learn.t, while grantToTenant (the WRITER) was never called by
# anything.  A function whose output is consumed by a well-tested function is
# not thereby tested.
#
# It is now honest: it records a NAME, the second argument is accepted and
# ignored for compatibility, and the doc says so.  Conferring a capability is
# comcon.grant(env, name, cap) / comcon.include(source, {grants}).
#
# What this pins:
#   - the name reaches tenantLearning().grants, which is the only thing that
#     ever consumed it and the reason not to delete the function outright;
#   - the legacy two-argument call still works, so existing host JS does not
#     break -- and is NOT validated, since checking a discarded value would
#     imply it is used;
#   - a missing or non-string name is refused rather than recorded.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

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
        location /g { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
/* Declared at config phase, where the registry lives (per-cycle). */
var OUT = { refused: [], accepted: [] };

function declare(label, args) {
    try {
        if (args.length === 1)      { nginx.grantToTenant(args[0]); }
        else if (args.length === 2) { nginx.grantToTenant(args[0], args[1]); }
        else                        { nginx.grantToTenant(); }
        OUT.accepted.push(label);
    } catch (e) {
        OUT.refused.push(label + ': ' + String(e.message).slice(0, 50));
    }
}

/* the one-argument form is the real shape now */
declare('one-arg', ['alpha']);

/* the legacy two-argument form still works.  The second argument is ignored
 * and NOT validated -- these are not sockets, and none of them may throw. */
declare('legacy-socket-shaped', ['beta', { notASocket: true }]);
declare('legacy-null',          ['gamma', null]);
declare('legacy-number',        ['delta', 12345]);

/* a name is required, and must be a string */
declare('no-args',      []);
declare('number-name',  [42, null]);

var locs = nginx.http.servers[0].locations;
for (var li = 0; li < locs.length; li++) {
    if (locs[li].path !== '/g') { continue; }
    locs[li].handler = function (req) {
        var out = { cfg: OUT };
        try {
            var learn = nginx.tenantLearning();
            out.grants = learn.grants;
            out.hasWants = Array.isArray(learn.wants);
            out.hasMode = (typeof learn.mode === 'string');
        } catch (e) { out.learnError = String(e && e.message); }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(out));
    };
}
JS

$t->try_run('no js module')->plan(9);

my $r = http_get('/g');
my ($body) = $r =~ /\r\n\r\n(.*)/s;
my $j = $body ? eval { decode_json($body) } : undef;

ok($j, 'probe responded') or diag substr($body // 'no body', 0, 200);

SKIP: {
    skip 'no response', 8 unless $j;

    my $cfg = $j->{cfg} || {};
    diag 'accepted: ' . join(', ', @{ $cfg->{accepted} || [] });
    diag 'refused:  ' . join(', ', @{ $cfg->{refused}  || [] });
    diag 'grants:   ' . join(', ', @{ $j->{grants} || [] });

    is(scalar @{ $cfg->{accepted} || [] }, 4,
       'the one-arg form and all three legacy two-arg calls were accepted')
        or diag 'a legacy caller would break';
    is(scalar @{ $cfg->{refused} || [] }, 2,
       'a missing name and a non-string name are both refused');

    # the point of keeping the function at all: the name reaches the report
    my %g = map { $_ => 1 } @{ $j->{grants} || [] };
    ok($g{alpha}, 'a declared name appears in tenantLearning().grants');
    ok($g{beta} && $g{gamma} && $g{delta},
       'names declared through the legacy form appear too');
    ok(!$g{42} && !exists $g{''},
       'a refused declaration records nothing');

    # the report itself still has its shape
    ok($j->{hasWants}, 'tenantLearning() still reports wants[]');
    ok($j->{hasMode},  'tenantLearning() still reports mode');
    ok(!$j->{learnError}, 'tenantLearning() did not throw')
        or diag $j->{learnError};
}
