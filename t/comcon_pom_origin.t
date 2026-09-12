#!/usr/bin/perl

# COMCON D5b-4 — cross-file provenance (POM.md §6 Q3).  A node has to be able to
# say WHERE it is, in a way another tool can use.  Before this, it could not:
#
#   comcon.pom(fn).line0        -> 18   (line in the enclosing FILE)
#   comcon.pom(fn).cst().line0  -> 1    (line within the NODE'S OWN source)
#
# The same field name meant two different things at the two tiers, with no file
# named anywhere -- so a denial record built from one and read as the other
# points at the wrong place and looks right.  Now every span says which base it
# counts in (`base:'node'` / `base:'file'`), bytecode spans carry `file` + `col0`
# (col0 was already computed in the engine and thrown away), and `origin()`
# converts a node-local span to an absolute one.
#
# THE EXPECTED LINE AND COLUMN ARE DERIVED FROM THE WRITTEN FILE, not hardcoded:
# the test greps root.js for the statement it is asking about.  A hardcoded 20
# would silently stop testing the mapping the first time anyone edits the fixture
# above that line -- which is precisely the failure mode this increment is about.
#
# `origin()` returns NULL when the origin is unknown, and an absolute `range`
# only when a byte offset was supplied.  Inventing a plausible location for a
# node whose origin is unknown is the source-map lie: a denial record naming the
# wrong file:line is worse than one that admits it does not know.

# NEGATIVE CONTROLS (run 2026-09-12; all nine reverted to failure, engine and
# tree rebuilt for each, and re-passed after restore):
#
#   span carries base:'node' (cst)                 -> tests 3-4 fail
#   origin() returns null when origin is unknown   -> test 11 fails
#   column shift applies to line 1 only            -> tests 7, 10, 12 fail
#   absolute range only when an offset is given    -> tests 8, 13 fail
#   cst() inherits the bytecode node's position    -> tests 2-16 fail
#   pom() refuses a bound wrapper                  -> test 16 fails
#   engine: POM span carries file + col0           -> tests 2, 6, 9 fail
#   fragment errors carry <comcon-fragment>:LINE   -> test 14 fails
#   include()'s wrapper preamble has NO newline    -> test 14 fails (line 4 -> 5)
#
# The last one had to be redone.  The first attempt added a newline to the
# preamble and broke tests 2-16 -- not from a line shift but because the buffer
# size was computed from a SEPARATE literal of the same text, so the wrapper
# overflowed its allocation by one byte.  A control that fires for the wrong
# reason is not a control.  The three pieces now derive from one definition
# (NGX_JS_COMCON_WRAP_*), and the length-preserving control (';' -> '\n') fails
# exactly test 14, reporting line 5 where the author wrote line 4.

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /o { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var l = locs.find(function (x) { return x.path === "/o"; });

l.handler = function (req) {
    var o = {};

    /* The subject.  Its inner statement is what the test locates in the file. */
    var host = function (b) {
        var q = 1;
        return q + b;
    };

    var pv = comcon.pom(host);
    var cv = pv.cst();

    /* the two bases, each saying which it is */
    o.pomSpan = pv.span;
    o.cstBase = cv.span.base;
    o.cstHasFile = (cv.span.file !== undefined);

    /* THE HOP: node-local -> absolute, for a statement inside the function */
    var stmts = cv.query('stmt');
    var last  = stmts[stmts.length - 1];
    o.localLine = last.line0;
    o.localCol  = last.span.col0;
    o.text      = cv.src.slice(last.range[0], last.range[1]);
    o.abs       = last.origin();

    /* line 1 of the node starts at the origin's column; a later line does not */
    o.rootAbs = cv.origin();
    o.col1shift = (cv.origin().col0 === pv.span.col0);
    o.colNoShift = (last.origin().col0 === last.span.col0);

    /* no origin => null, never a guess */
    o.plain = comcon.cst("function(){return 1;}").origin();

    /* an explicit origin, the §38 shape: you have vendor TEXT and you know
     * where it came from.  With an offset, an absolute range is available. */
    var vend = comcon.cst("function(){\n  return fetch(1);\n}",
                          { file: "vendor/a.js", line0: 41, col0: 4,
                            offset: 900 });
    var call = vend.query('call(fetch)')[0];
    o.vendOrigin = call.origin();
    o.vendNoOffset = comcon.cst("function(){\n  return fetch(1);\n}",
                                { file: "vendor/a.js", line0: 41 })
                       .query('call(fetch)')[0].origin();

    /* THE INCLUDE HOP: a fragment's synthetic file origin, and the line.
     * `null.k` is on line 4 of SRC; include()'s wrapper preamble carries no
     * newline, so the reported line must be the AUTHOR's line 4. */
    var SRC = "function (a) {\n"
            + "  var x = 1;\n"
            + "  var y = 2;\n"
            + "  null.k;\n"
            + "  return x + y;\n"
            + "}";
    var f = comcon.include(SRC, { imports: [] });
    o.fragErr = 'not-thrown';
    try { f(0); } catch (e) { o.fragErr = String(e.message); }

    /* the bound wrapper is NOT the fragment: refuse, loudly */
    o.wrapper = 'ACCEPTED';
    try { comcon.pom(f); }
    catch (e) { o.wrapper = /BOUND WRAPPER/.test(e.message) ? 'refused' : e.message; }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(16);

###############################################################################

# What the file actually says — the expectation is read off the fixture, so an
# edit above the subject cannot quietly invalidate this test.
my $root = $t->read_file('root.js');
my @lines = split /\n/, $root, -1;
my ($want_line, $want_col);
for my $i (0 .. $#lines) {
    next unless $lines[$i] =~ /^(\s*)return q \+ b;/;
    $want_line = $i + 1;           # 1-based
    $want_col  = length($1);       # 0-based column
    last;
}
ok(defined $want_line, "found the subject statement in root.js (line $want_line)");

my $r = http_get('/o');

# --- the two bases now say what they are ---------------------------------
like($r, qr/"pomSpan":\{"base":"file","file":"[^"]*root\.js","line0":\d+/,
     'a bytecode-tier span is FILE-relative and carries the file');
like($r, qr/"cstBase":"node"/, 'a cst() span says it is NODE-local');
like($r, qr/"cstHasFile":false/,
     '...and carries no file, so it cannot be misread as absolute');

# --- the hop, against the real file --------------------------------------
like($r, qr/"text":"return q \+ b;"/, 'the subject statement is the one asked about');
like($r, qr/"abs":\{"base":"file","file":"[^"]*root\.js","line0":$want_line,/,
     "origin() maps node-local line to the FILE line ($want_line)");
like($r, qr/"abs":\{[^}]*"col0":$want_col,/,
     "...and to the file column ($want_col), which is the real indentation");
like($r, qr/"abs":\{[^}]*"range":null/,
     'no absolute range is reported, because the bytecode tier carries no '
     . 'byte offset -- null, not a guess');

# --- column arithmetic applies to line 1 only ----------------------------
like($r, qr/"col1shift":true/,
     "the node's first line starts at the origin's column");
like($r, qr/"colNoShift":true/,
     '...a later line does not (it starts at its own column 0)');

# --- unknown origin is null ----------------------------------------------
like($r, qr/"plain":null/,
     'a view with no origin returns null rather than inventing a location');

# --- an explicit origin, with and without an offset ----------------------
# Same discipline as above: compute what the answer must be from the fixture
# text, with perl's index(), instead of trusting arithmetic done by hand.
my $vsrc  = "function(){\n  return fetch(1);\n}";
my $voff  = 900;
my $vline = 41;
my $vcol  = 4;
my $at    = index($vsrc, 'fetch(1)');
my $nl    = index($vsrc, "\n");
my $ecol  = $at - $nl - 1;                      # column on its own line
my ($er0, $er1) = ($voff + $at, $voff + $at + length('fetch(1)'));
my $eline = $vline + 1;                         # it is on the node's 2nd line
like($r, qr/"vendOrigin":\{"base":"file","file":"vendor\/a\.js","line0":$eline,"line1":$eline,"col0":$ecol,"col1":@{[$ecol + length('fetch(1)')]},"range":\[$er0,$er1\]\}/,
     "an explicit origin maps a node to file:line:col ($eline:$ecol) AND an "
     . "absolute range ([$er0,$er1])");
like($r, qr/"vendNoOffset":\{[^}]*"range":null/,
     '...and omits the range when no offset was supplied');

# --- the include hop ----------------------------------------------------
like($r, qr/"fragErr":"comcon: fragment: TypeError[^"]*at <comcon-fragment>:4/,
     'a fragment failure names the synthetic file origin AND the line');
like($r, qr/"fragErr":"comcon: fragment: TypeError: cannot read property/,
     '...without losing the original message');

# --- the wrapper is not the fragment ------------------------------------
like($r, qr/"wrapper":"refused"/,
     "pom() refuses a confined fragment's bound wrapper instead of describing "
     . 'the wrapper and answering every query about it');
