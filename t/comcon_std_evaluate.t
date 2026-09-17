#!/usr/bin/perl

# COMCON v5.127 -- SHOWCASE-gaps G-13: comcon.std.evaluate(fn | source, opts),
# the five-minute vendor evaluation as ONE static read.
#
# admit() stops at the FIRST undeclared free name, because one is enough to
# refuse.  An evaluation wants all of them: every free name, classified
# (intrinsic / authority / denied), with its call sites and lines from the
# bytecode (D5a), the dynamic-code flag, and the undocumented remainder against
# what the vendor's docs claim.  The report runs NOTHING: a source is parsed by
# the vendored acorn first and accepted only as exactly one function expression
# (trailing text is refused -- `function(){} ; evil()` would otherwise run evil()
# on the host while being "compiled"), and the free-name collector is the same
# C walk admission uses, so the two cannot disagree about what a fragment names.
#
# NEGATIVE CONTROLS (run while writing; each restored):
#   - the trailing-text check removed -> test 9 fails (the appended statement
#     runs, and 'ran' shows up in the host-side marker)
#   - `declares` ignored -> test 5 fails
#   - the dynamic-code flag dropped from the verdict -> test 7 fails

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
        location /e { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* the vendor SDK, as a host-side function: two fetch call sites (one with a
   nested argument), a bare reference, an intrinsic, and a reach for nginx */
var sdk = function (x) {
    var a = fetch("https://api.partner.com/v1");
    var b = fetch(helper(2));
    var g = fetch;
    var c = JSON.stringify(x);
    nginx.log(1, "x");
    return a + b + c + typeof g;
};

var dyn = function (x) { return eval("1+" + x); };

var MARK = 'not-run';
globalThis.__evalMarker = function () { MARK = 'ran'; };

locs.find(function (l) { return l.path === "/e"; }).handler = function (req) {
    var o = {};
    var r = comcon.std.evaluate(sdk, { declares: ["fetch"] });
    o.authorities = r.authorities;
    o.intrinsics = r.intrinsics;
    o.undocumented = r.undocumented;
    o.fetch = r.names.filter(function (n) { return n.name === 'fetch'; })[0];
    o.admissible = r.admissible;
    o.verdict = r.verdict;
    o.contract = r.contract;

    var d = comcon.std.evaluate(dyn);
    o.dynamicCode = d.dynamicCode;
    o.dynAdmissible = d.admissible;
    o.dynVerdict = d.verdict;

    /* a source string is accepted only as one function expression */
    o.src = comcon.std.evaluate("function(req){ return req.uri + Math.max(1, 2); }").verdict;
    try { comcon.std.evaluate("function(){ return 1; }; __evalMarker()"); o.trailing = 'ACCEPTED'; }
    catch (e) { o.trailing = /trailing text/.test(e.message) ? 'refused' : 'threw: ' + e.message; }
    o.marker = MARK;
    try { comcon.std.evaluate("var x = 1"); o.notFn = 'ACCEPTED'; }
    catch (e) { o.notFn = 'refused'; }
    try { comcon.std.evaluate(42); o.number = 'ACCEPTED'; }
    catch (e) { o.number = 'refused'; }

    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(12);

my $b = http_get('/e');

like($b, qr/"authorities":\["fetch","helper","nginx"\]/,
     'every free name that is not an intrinsic, in one read');
like($b, qr/"intrinsics":\["JSON"\]/,
     'intrinsics classified apart: a value to compute with, not authority');
like($b, qr/"undocumented":\["helper","nginx"\]/,
     'the remainder against what the vendor declared');
like($b, qr/"fetch":\{"name":"fetch","kind":"authority","calls":2,"lines":\[\d+,\d+\],"references":3\}/,
     'per name: two call sites with lines (the nested argument attributed), three references');
like($b, qr/"verdict":"requests 3 authorities \(fetch, helper, nginx\); declared 1 of 3"/,
     'the verdict a procurement reviewer reads');
like($b, qr/"contract":\{"imports":\["fetch","helper","nginx"\]\}/,
     'and the imports line a contract would need, ready to paste');
like($b, qr/"dynamicCode":true,"dynAdmissible":false,"dynVerdict":"[^"]*uses dynamic code, refused at admission"/,
     'dynamic code is flagged and named in the verdict');
like($b, qr/"src":"requests 1 authority \(Math\)"/,
     'a source string is evaluated the same way');
like($b, qr/"trailing":"refused","marker":"not-run"/,
     'a source with text after the function expression is refused, and the text never ran');
like($b, qr/"notFn":"refused"/, 'a source that is not a function expression is refused');
like($b, qr/"number":"refused"/, 'a non-function is refused');
like($b, qr/"admissible":true/, 'the SDK itself is admissible once its names are declared');
