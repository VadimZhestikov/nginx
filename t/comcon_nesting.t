#!/usr/bin/perl

# DO CAGES NEST?  SHOWCASE17 §8 says they do "for free".
#
# That scenario -- "Resellers: your tenant becomes a host" -- headlines the
# multi-party case: ACME resells to its own customers and cages them itself,
# "no involvement from the platform team", by calling env/grant/mediate/include
# from inside its own code.  The file is marked illustrative and its syntax
# hypothetical, but the claim is load-bearing enough to be worth measuring rather
# than assumed, because a reader plans around it.
#
# IT IS THE OPPOSITE OF SOMETHING THE ESCAPE GATE ASSERTS.  `t/comcon_mses_gate.t`
# probes `typeof comcon` inside a fragment and requires it CLOSED -- reaching the
# kernel operators is an escape, by design.  So nesting cannot be "free": it would
# need the operators deliberately re-exposed to a confined fragment, which is
# exactly what INCREMENT_MLIB §4 calls "raw operators withheld" and does not build.
#
# WHAT THIS FILE PINS is the precise line between what nests and what does not, so
# the doc set can say it and stop implying more:
#
#   ATTENUATION nests, without limit, from the host side.
#   AUTHORING does not nest at all.
#
# A platform can build a chain of cages as deep as it likes and hand the innermost
# to a fragment.  A fragment cannot build one, cannot attenuate what it holds, and
# cannot pass anything to anybody -- it has no operator to do it with, and the one
# deliberate attempt (granting `comcon` itself) is refused at admission.

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /nest { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

locs.forEach(function (l) {
    if (l.path !== '/nest') { return; }
    l.handler = function (req) {
        var o = {};
        try {
            comcon.mode('enforce');

            /* (1) the kernel operators are simply not there.  No contract, so no
             * admission gate and no free-name refusal: this is a real read of the
             * compartment's global, not a rejection. */
            o.bare = comcon.include(
                "function(a){ return { comcon: typeof comcon,"
              + " nginx: typeof nginx }; }", {})({});

            /* (2) DECLARING the name does not conjure it.  `imports` is a
             * whitelist of free names, not a set of capabilities -- the two halves
             * an operator most often confuses. */
            o.declared = comcon.include(
                "function(a){ return typeof comcon; }",
                { imports: ['comcon'] })({});

            /* (3) and it cannot be GRANTED deliberately either: what may cross
             * into a compartment is exactly what the host can WRAP, and the kernel
             * operator table is not a C-backed capability. */
            o.granted = 'ACCEPTED';
            try {
                comcon.include("function(a){ return typeof k; }",
                               { imports: [], grants: { k: comcon } });
            } catch (e) { o.granted = e.code || e.name; }

            /* (4) THE POSITIVE CONTROL -- attenuation nests, and deeply.  The host
             * builds a three-level chain and hands the innermost to a fragment;
             * each level may only narrow, which is V4's meet.  Without this the
             * assertions above would read as "nothing works here". */
            var m1 = comcon.mediate(sock, comcon.allow(['port', 'address']));
            var m2 = comcon.mediate(m1, comcon.allow(['port']));
            var m3 = comcon.mediate(m2, comcon.uses('nest:probe', 5, 60));
            var deep = comcon.include(
                "function(a){ return { port: typeof s.port,"
              + " address: typeof s.address }; }",
                { imports: [], grants: { s: m3 } });
            o.chain = deep({});

            /* (5) ...and the chain really is a chain: level 2 hid `address`, so the
             * innermost cannot see it however the host got there. */
            o.narrowed = (o.chain.address === 'undefined');

        } catch (e) {
            o.driverError = String(e && e.message)
                             + ' @ ' + String(e && e.stack).split('\n')[0];
        }
        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(8);

my $raw = http_get('/nest');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 400)); $o = {};
};

is($o->{driverError}, undef, 'the nesting probe ran') or diag($o->{driverError});
diag("bare: " . encode_json($o->{bare} || {}) . "  declared: "
     . ($o->{declared} // '?') . "  granted: " . ($o->{granted} // '?')
     . "  chain: " . encode_json($o->{chain} || {}));

is($o->{bare}{comcon}, 'undefined',
   'THE KERNEL OPERATORS ARE NOT IN A FRAGMENT. No contract here, so no admission '
   . 'gate and no free-name refusal -- this is a real read of the compartment '
   . 'global. `comcon.include` is not something a fragment can call, so it cannot '
   . 'cage anybody');
is($o->{bare}{nginx}, 'undefined',
   '...and neither is the COM root, for the same reason');

is($o->{declared}, 'undefined',
   'DECLARING THE NAME DOES NOT CONJURE IT: `imports` is a whitelist of free '
   . 'names, not a set of capabilities. Admission stops objecting; nothing '
   . 'appears');

is($o->{granted}, 'E_CAP_GRANT',
   'AND IT CANNOT BE GRANTED DELIBERATELY: what may cross into a compartment is '
   . 'exactly what the host can WRAP, and the kernel operator table is not a '
   . 'C-backed capability. So re-exposing the operators is not a configuration '
   . 'choice an operator has today -- it needs a mechanism that does not exist');

# --- the positive control: attenuation DOES nest ---
is($o->{chain}{port}, 'number',
   'ATTENUATION NESTS: the host built a three-level chain -- allow(port,address), '
   . 'then allow(port), then uses() -- and the fragment holding the innermost can '
   . 'still read what every level left it');
ok($o->{narrowed},
   '...and each level only NARROWED: `address`, hidden at level 2, is invisible '
   . 'at level 3 however the host got there. Without this pair the refusals above '
   . 'would read as "nothing works here" rather than as a precise boundary');

# --- the statement the docs may now make ---
ok($o->{bare}{comcon} eq 'undefined' && $o->{chain}{port} eq 'number',
   'SO: ATTENUATION NESTS WITHOUT LIMIT; AUTHORING DOES NOT NEST AT ALL. A '
   . 'platform can build cages as deep as it likes and hand the innermost to a '
   . 'fragment; a fragment can build none, attenuate nothing it holds, and pass '
   . 'nothing to anybody. SHOWCASE17 §8\'s "cages nest for free" is true of the '
   . 'first half and false of the second, and the second is the half it shows');

$t->stop();
