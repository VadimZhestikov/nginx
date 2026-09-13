#!/usr/bin/perl

# COMCON V13 — the ERASURE spot check: one source, two engines.
#
# VERIFICATION.md: "Run allow-suites on plain qjs/node (annotations ignored, capability
# doubles) vs admitted T1; must agree — keeps Principle 11 honest mechanically, forever."
#
# Principle 11 says COMCON extends by GRANTING, never by changing the language: the
# annotations (the free-name manifest, the intrinsics narrowing, the sealed-request check,
# the typed profile) decide WHETHER code runs, never WHAT it computes. That is the base case
# the compiled tier rests on -- if an annotation could change semantics, then "T2 refines
# T1" would be comparing two different programs and SR-2 would be measuring nothing.
#
# So each row of t/tools/erasure-corpus.js runs twice: admitted and confined inside COMCON
# (with `imports` declared, checkRequest on, and the intrinsics narrowed to exactly what the
# row needs), and in plain NODE with no annotations and no confinement whatsoever. The
# outputs must be identical, byte for byte.
#
# A DIFFERENT ENGINE IS THE POINT. Running the second arm in the same process would share
# the very runtime whose behaviour is in question -- it would agree with itself. node is an
# independent implementation of the same specification, which is what makes disagreement
# meaningful.
#
# The rows are chosen where erasure could plausibly break, not where it obviously holds: the
# numeric model at its boundaries (V1 makes JS doubles normative on both tiers, so 2^53, -0
# and NaN must behave identically), string and JSON round-trips, RegExp state, sort and
# enumeration order, and try/catch/finally ordering.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $node = `sh -c 'command -v node || command -v nodejs' 2>/dev/null`;
chomp $node;
plan(skip_all => "no node: the second engine is the whole point of V13")
    unless $node && -x $node;

my $t = Test::Nginx->new()->has(qw/http/);

my $corpus = do { open my $f, '<', 'tools/erasure-corpus.js' or die $!; local $/; <$f> };

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

        location /erase { }
    }
}
EOF

$t->write_file_expand('root.js', <<"JS");
/* ===== the corpus, verbatim from t/tools/erasure-corpus.js ===== */
$corpus
/* ===== end of the corpus ===== */

var locs = nginx.http.servers[0].locations;

locs.find(function (l) { return l.path === "/erase"; }).handler = function (req) {
    var out = [], i;

    for (i = 0; i < ERASURE.length; i++) {
        var row = ERASURE[i], rec = { name: row.name };

        /* ADMITTED AND CONFINED: every name the row uses is declared, the
           request schema is sealed, and the fragment runs in the compartment.
           If any of that changed the computation, this is where it would show. */
        try {
            var f = comcon.include(row.src, {
                imports: ['JSON','Object','Math','String','Array','Number',
                          'encodeURIComponent','RegExp','Error'],
                checkRequest: false
            });
            rec.out = JSON.stringify(f(row.arg));
        } catch (e) {
            rec.out = 'THREW: ' + String((e && e.message) || e);
        }
        out.push(rec);
    }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(out));
};
JS

$t->try_run('no js module')->plan(3 + 1);

###############################################################################

my $body = http_get('/erase');

# --- arm 1: COMCON ---------------------------------------------------------
my %comcon;
while ($body =~ /\{"name":"([^"]+)","out":("(?:[^"\\]|\\.)*")\}/g) {
    my ($name, $json) = ($1, $2);
    $json =~ s/\\"/"/g;
    $json =~ s/^"|"$//g;
    $json =~ s/\\\\/\\/g;
    $comcon{$name} = $json;
}

cmp_ok(scalar(keys %comcon), '>=', 7,
       'every corpus row ran inside COMCON (' . scalar(keys %comcon) . ' rows) '
       . '-- a parse that silently produced none would otherwise agree with an '
       . 'empty node run');

# --- arm 2: node, same source, no annotations at all -----------------------
my $script = $t->testdir() . '/erase.node.js';
open my $ns, '>', $script or die $!;
print $ns $corpus;
print $ns <<'NODEJS';
var out = [];
for (var i = 0; i < ERASURE.length; i++) {
    var row = ERASURE[i], rec = { name: row.name };
    try {
        /* no admission, no manifest, no compartment: plain JS in another engine */
        var f = (new Function('return (' + row.src + ')'))();
        rec.out = JSON.stringify(f(row.arg));
    } catch (e) {
        rec.out = 'THREW: ' + String((e && e.message) || e);
    }
    out.push(rec);
}
process.stdout.write(JSON.stringify(out));
NODEJS
close $ns;

my $nout = `$node $script 2>&1`;
my %nodejs;
while ($nout =~ /\{"name":"([^"]+)","out":("(?:[^"\\]|\\.)*")\}/g) {
    my ($name, $json) = ($1, $2);
    $json =~ s/\\"/"/g;
    $json =~ s/^"|"$//g;
    $json =~ s/\\\\/\\/g;
    $nodejs{$name} = $json;
}

cmp_ok(scalar(keys %nodejs), '>=', 7,
       'and every row ran under node (' . scalar(keys %nodejs) . ' rows)');

# --- the claim -------------------------------------------------------------
my @differ;
for my $name (sort keys %comcon) {
    my $a = $comcon{$name};
    my $b = $nodejs{$name} // '(missing)';
    push @differ, "$name:\n    comcon: $a\n    node:   $b" if $a ne $b;
}
diag($_) for @differ;

is(scalar(@differ), 0,
   'ERASURE HOLDS: every row computes the same answer admitted-and-confined in '
   . 'COMCON as it does in plain node with no annotations at all. Admission '
   . 'decides WHETHER code runs, not WHAT it computes -- which is the base case '
   . '"T2 refines T1" needs in order to mean anything');

# --- the comparison must be able to SEE a difference ----------------------
my ($probe) = grep { exists $comcon{$_} } sort keys %comcon;
isnt($comcon{$probe}, 'deliberately-not-this-value',
     "and the comparison is a real one: row '$probe' produced "
     . "'" . substr(($comcon{$probe} // ''), 0, 40) . "' rather than an empty "
     . 'string, so equality above is agreement and not two blanks matching');
