#!/usr/bin/perl

# A FRAGMENT'S FAILURE REACHES THE HOST AS WHAT IT IS.
#
# Two defects, found while looking at why a memory probe reported `undefined`:
#
# 1. OUT OF MEMORY READ AS `null`.  At a hard memory limit the engine cannot
#    allocate the InternalError it means to throw, so JS_ThrowError2() throws
#    JS_NULL instead.  The host then reported
#
#        comcon: fragment: null
#
#    which is byte-for-byte what a fragment doing `throw null` produces.  So an
#    operator could not tell "this tenant exhausted its memory allowance" from
#    "this tenant threw null" -- and the first is the one the per-invocation
#    allowance exists to report.  The vendored engine now counts its
#    out-of-memory throws (JS_GetOutOfMemoryCount), and a non-object exception
#    thrown while that count moved is named as the allocation failure it is.
#
# 2. AN EXCEPTION DURING include() ARRIVED WITH NO VALUE AT ALL.  The fragment's
#    expression is evaluated by a call on the COMPARTMENT context, and its
#    exception path was `return fn` -- JS_EXCEPTION, which means "an exception is
#    pending ON THIS CONTEXT", returned to the HOST, where nothing was pending.
#    The host caught `typeof e === "unknown"`, `String(e)` = "[unsupported
#    type]": no message, no code, no Error.  Anything thrown while the expression
#    was evaluated took that path -- a top-level `throw` as much as an
#    out-of-memory -- and the real exception stayed pending in the compartment.
#
# Test 2 is the control that makes test 3 mean something: `throw null` must STILL
# read as null, or "out of memory" would just be what every null now says.

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

worker_processes 1;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /report { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var l = nginx.http.servers[0].locations[0];

function caught(f) {
    try { return { returned: String(f()) }; }
    catch (e) {
        return { type: typeof e, isError: e instanceof Error,
                 message: (e && typeof e.message === 'string') ? e.message : null };
    }
}

l.handler = function (req) {
    var o = {};
    try {
        comcon.mode('enforce');

        o.throwNull = caught(function () {
            return comcon.include("function(a){ throw null; }", { imports: [] })({});
        });

        o.callOOM = caught(function () {
            return comcon.include(
                "function(a){ var k = [], i; for (i = 0; i < 1e8; i++) { k.push({ i: i }); }"
              + " return k.length; }", { imports: [] })({});
        });

        o.includeOOM = caught(function () {
            return comcon.include(
                "(function(){ var k = [], i; for (i = 0; i < 3e6; i++) { k.push({ i: i }); }"
              + " return function(a){ return k.length; }; })()", { imports: [] });
        });

        o.topThrow = caught(function () {
            return comcon.include(
                "(function(){ throw new Error('thrown-at-top-level'); })()",
                { imports: [] });
        });

        o.after = caught(function () {
            return comcon.include("function(a){ return 'alive'; }", { imports: [] })({});
        });

    } catch (e) {
        o.driverError = String(e && e.message);
    }
    req.respond(200, { 'content-type': 'application/json' }, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(7);

my $raw = http_get('/report');
$raw =~ s/^.*?\r\n\r\n//s;
my $o = eval { decode_json($raw) } || { driverError => 'non-JSON: ' . substr($raw, 0, 200) };

is($o->{driverError}, undef, 'the error-report probe ran')
    or diag("driverError: $o->{driverError}");

like($o->{throwNull}{message} // '', qr/^comcon: fragment: null$/,
   'CONTROL: a fragment that really throws null still reports null -- so the '
   . 'next assertion is about the allocation failure, not about every null')
    or diag('throwNull: ' . encode_json($o->{throwNull} || {}));

like($o->{callOOM}{message} // '', qr/out of memory.*allowance/,
   'A FRAGMENT THAT EXHAUSTS ITS ALLOWANCE SAYS SO. At the limit the engine '
   . 'cannot allocate its InternalError and throws null, so this used to read '
   . '"comcon: fragment: null" -- identical to the control above')
    or diag('callOOM: ' . encode_json($o->{callOOM} || {}));

ok(($o->{includeOOM}{isError} // 0) && ($o->{includeOOM}{message} // '') =~ /out of memory/,
   'AN include() THAT RUNS OUT OF MEMORY THROWS A REAL ERROR NAMING IT. Before: '
   . 'typeof "unknown", String() "[unsupported type]" -- the exception was pending '
   . 'on the compartment context and JS_EXCEPTION was returned to the host')
    or diag('includeOOM: ' . encode_json($o->{includeOOM} || {}));

ok(($o->{topThrow}{isError} // 0)
   && ($o->{topThrow}{message} // '') =~ /thrown-at-top-level/,
   '...and it was never specific to memory: a plain throw while the fragment\'s '
   . 'expression is evaluated now reaches the host with its message, where it '
   . 'used to arrive with no value at all')
    or diag('topThrow: ' . encode_json($o->{topThrow} || {}));

is($o->{after}{returned}, 'alive',
   'and the compartment keeps working after all four');

unlike($t->read_file('error.log'), qr/\[unsupported type\]/,
   'nothing in the log carries the empty-exception rendering either');
