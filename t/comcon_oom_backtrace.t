#!/usr/bin/perl

# F18 -- an out-of-memory INSIDE the engine's backtrace annotation freed the
# pending exception under its own feet (a fragment-reachable worker SIGSEGV).
#
# The mechanism, in the engine: an error thrown from bytecode is created WITHOUT
# its 'stack' (JS_ThrowError defers the backtrace); the interpreter's exception
# label then calls build_backtrace(ctx, rt->current_exception, ...) -- by value,
# holding no reference. build_backtrace allocates (the frame strings, the 'stack'
# string, the property slot). At the memory allowance any of those fails and
# throws out-of-memory, and JS_Throw() FREES the pending exception -- the very
# error object being annotated, whose only reference it was. build_backtrace
# then defines 'stack' on a freed object: find_own_property on a NULL shape.
#
# Reachable from a fragment: exhaust the allowance so that the OOM InternalError
# itself is still buildable (a couple of hundred bytes) but its backtrace is not.
# Where the allowance bites is a matter of residue, so the probe SWEEPS it: the
# fragment fills memory in exact 1 KB strings and the allowance steps by 32
# bytes across one such string, walking the failing allocation through every
# alignment -- including the window where the error exists and the backtrace
# does not. Before the fix that window killed the worker on this layout 3/3;
# the sweep is what keeps the probe honest on any other.
#
# The fix (quickjs.c build_backtrace_pending): hold a reference across the
# annotation; if the attempt threw, put the original error -- and its
# uncatchable flag -- back, minus its 'stack'. The fragment's catch then sees
# the out-of-memory error it was owed, not a dead object and not a null.

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

        location /sweep { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

/* Fill in exact 1 KB strings until the allowance refuses one. The catch runs
   AFTER the engine's backtrace attempt (the exception label annotates before it
   unwinds), so the probe is the throw itself; the catch only reports what
   arrived: the error the engine built, or null when even that could not be. */
var FILL =
    "function(){ var a = [];"
  + "  try { for (;;) { a.push('x'.repeat(1024)); } }"
  + "  catch (e) { a.length = 0;"
  + "    return e === null ? 'null' : ('' + e.message); } }";

locs.find(function (l) { return l.path === "/sweep"; }).handler = function (req) {
    var i, frag, out = { attempts: 0, oom: 0, nul: 0, other: [] };

    for (i = 0; i < 32; i++) {
        frag = comcon.include(FILL,
            { imports: [],
              meter: comcon.meter({ memoryBytes: 1048576 + i * 32 }) });
        out.attempts++;
        try {
            var r = frag();
            if (r === 'null') { out.nul++; }
            else if (r === 'out of memory') { out.oom++; }
            else { out.other.push(r); }
        } catch (e) {
            /* the catch inside the fragment ran out of memory itself; the host
               sees an ordinary failure, not a dead worker */
            out.other.push('THREW ' + String((e && e.message) || e).slice(0, 60));
        }
    }

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(out));
};
JS

$t->try_run('no js module')->plan(5);

###############################################################################

my $r = http_get('/sweep');
diag($1) if $r =~ /(\{.*\})/;

like($r, qr/^HTTP\/1.1 200/,
     'the sweep completes: 32 out-of-memory throws at 32 alignments of the '
     . 'allowance, and the worker answers -- before the fix one of them was a '
     . 'SIGSEGV in find_own_property on the freed pending exception');
like($r, qr/"attempts":32/, 'every alignment was tried');
like($r, qr/"oom":[1-9]\d*/,
     'the fragment\'s catch received the out-of-memory ERROR in at least one '
     . 'alignment: the object the engine built and then failed to annotate is '
     . 'handed to the catch intact (minus its stack), not freed under it');

# the compartment is healthy afterwards: the same sweep again, new slots
my $r2 = http_get('/sweep');
like($r2, qr/"attempts":32/, 'a second sweep runs to completion as well');

# the only other outcome allowed: the fragment's OWN catch ran out of memory
# building its answer, which the host reports as an ordinary fragment failure
my @other = map { /"other":\[([^\]]*)\]/ ? split(/,/, $1) : () } ($r, $r2);
is(scalar(grep { !/^"THREW comcon: fragment: out of memory/ } @other), 0,
   'nothing else came back: every attempt ended in the fragment\'s catch with '
   . 'the error or a null, or in the host\'s ordinary out-of-memory failure '
   . 'when that catch could not build its answer -- never in a dead worker');

###############################################################################

undef $t;
