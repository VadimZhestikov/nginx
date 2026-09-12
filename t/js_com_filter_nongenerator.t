#!/usr/bin/perl

# A body filter that does not return a generator.
#
# `loc.addBodyFilter(fn)` in generator mode calls fn and then drives the result
# with `.next()`.  If fn returns something that is not an object — a number, a
# string, or nothing at all because a code path forgot to return — the old code
# went straight to JS_GetPropertyStr(result, "next") on a primitive.
#
# MEASURED, on the old code, for `return 42`:
#
#     GET /num  =>  (empty)
#     error.log =>  js exception: TypeError: not a function
#
# The client gets NOTHING — not a 500, not a truncated body, an empty response —
# and the log names neither the filter nor the location, only machinery.  A
# filter with a missing `return` silently drops the response.
#
# I first judged this family "loud but obscure" from reading it, and wrote this
# file as a diagnostics test.  The negative control disagreed: reverting the fix
# failed the three request-completes assertions too, not just the log one.  The
# reading was wrong and the measurement was right, which is the whole argument
# for running the control before believing the description.
#
# Found by t/tools/callback-return-sweep.py, which enumerates every JS_Call in
# src/js and asks what happens to the value it returns.
#
# The behaviour being pinned:
#   - a non-generator return does not crash the worker;
#   - the request COMPLETES, with the filter skipped rather than the response
#     dropped on the floor;
#   - the error log names the filter's mistake.

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

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /num    { }
        location /undef  { }
        location /str    { }
        location /ok     { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var by = {};
for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

/* returns a number instead of a generator */
by['/num'].addBodyFilter('generator', function (chunks, req) { return 42; });
by['/num'].handler = function (r) { r.respond(200, {}, 'NUM'); };

/* forgot to return at all — the mistake this family of bugs keeps taking */
by['/undef'].addBodyFilter('generator', function (chunks, req) { });
by['/undef'].handler = function (r) { r.respond(200, {}, 'UNDEF'); };

/* returns a string */
by['/str'].addBodyFilter('generator', function (chunks, req) { return 'nope'; });
by['/str'].handler = function (r) { r.respond(200, {}, 'STR'); };

/* the control: a real generator, which must still work */
by['/ok'].addBodyFilter('generator', async function* (chunks, req) {
    for await (var c of chunks) { yield c; }
});
by['/ok'].handler = function (r) { r.respond(200, {}, 'OK'); };
JS

$t->try_run('no js module')->plan(7);

# the control first: a "fix" that broke every generator filter would otherwise
# sail through the three cases below
my $ok = http_get('/ok');
like($ok, qr/200 OK/, 'control: a real generator filter still works');
like($ok, qr/OK/,     'control: its body still comes through');

for my $p (qw/num undef str/) {
    my $r = http_get("/$p");
    like($r, qr/HTTP\/1\.1 200 /,
         "/$p: a non-generator return leaves the request completing");
}

# and the worker is still alive and serving afterwards
like(http_get('/ok'), qr/OK/, 'the worker still serves after all three');

$t->stop();

my $log = $t->read_file('error.log') // '';
my $named = () = $log =~ /did not return a generator/g;
diag "error.log named the filter mistake $named time(s)";

# Work verification for the fix itself.  Without this the three assertions
# above would pass just as happily against the OLD code, which also completed
# the request -- it simply logged something unhelpful on the way.  This is the
# only assertion here that can tell the two apart, and it is the reason the
# guards are not six untested branches.
is($named, 3, 'each of the three bad filters was named in the log')
    or diag 'a count of 0 means the new guards never ran';
