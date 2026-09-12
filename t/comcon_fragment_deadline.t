#!/usr/bin/perl

# COMCON: a confined fragment is ALWAYS bounded, even with no meter contract.
#
# comcon.include reads contract.meter[...].timeoutMs and hands it to
# __invokeConfined, which used to arm the worker deadline only when that value
# was > 0. With no meter -- the default -- a fragment ran with NO bound at all,
# so an accidental infinite loop in confined code hung the worker without any
# escape being involved. ngx_js_comcon_invoke_confined now applies
# NGX_JS_COMCON_FRAGMENT_TIMEOUT_MS when the contract asks for nothing.
#
# THIS FILE MUST NOT SET nginx.workerRequestTimeout. The per-request deadline is
# computed at REQUEST ENTRY from that property, so a file that sets it -- like
# t/comcon_mses_gate.t, which arms the guard to test the guard -- already has a
# deadline in force and cannot observe the fragment's own default. Setting the
# property inside the handler is too late for the same reason. That is why this
# lives in its own file, and why it is the only assertion here.

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

        location /nometer { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
/* No contract.meter, and the host never sets nginx.workerRequestTimeout, so
 * the ONLY thing that can stop this is the bound comcon.include applies.
 *
 * SIZED FROM MEASUREMENT, twice over: 4e9 iterations run ~4.4s here, which is
 * INSIDE the 5s default -- so a first version of this test could not see the
 * bound working and passed for an unrelated reason. 9e9 runs ~10s, comfortably
 * past it. Finite on purpose: a regression is a failed assertion ~10s late,
 * never a hung suite. */
var frag = comcon.include(
    "function(req){" +
    "  var s = 0;" +
    "  for (var i = 0; i < 9000000000; i++) { s += i % 7; }" +
    "  return { status: 200, body: 'completed s=' + s }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/nometer") {
        locs[i].handler = function (req) {
            var t0 = Date.now();
            try {
                var o = frag({ method: req.method });
                req.respond(200, {'content-type':'text/plain'},
                            'RAN-TO-END ' + (Date.now() - t0) + 'ms ' + o.body);
            } catch (e) {
                req.respond(200, {'content-type':'text/plain'},
                            'STOPPED ' + (Date.now() - t0) + 'ms '
                            + String(e && e.message || e));
            }
        };
    }
}
JS

$t->try_run('no js module')->plan(3);

###############################################################################

my $r = http_get('/nometer');

# STOPPED / RAN-TO-END are disjoint. The first attempt here used
# BOUNDED / UNBOUNDED -- and "UNBOUNDED " CONTAINS "BOUNDED ", so the test
# passed while the fragment ran to completion, in BOTH the fixed and the
# control build. That is the second time in this file's history that a marker
# pair where one string contains the other produced a false pass; check the
# tokens are disjoint, not merely different.
like($r, qr/STOPPED /,
     'a fragment with no meter contract is still bounded')
    or diag("probe said: " . (($r =~ /\r\n\r\n(.*)/s)[0] // '(no body)'));

my ($ms) = $r =~ /STOPPED (\d+)ms/;
cmp_ok($ms // 0, '>=', 1000,
       'it ran until a deadline, not an unrelated immediate throw');
cmp_ok($ms // 999999, '<', 9000,
       'and it was stopped before the loop would have ended on its own');
