#!/usr/bin/perl

# F16 -- A COMPILED FRAGMENT COULD CATCH ITS OWN DEADLINE.
#
# The deadline is the engine's interrupt, thrown UNCATCHABLE: JS_CallInternal's
# exception path skips every catch handler while the flag is set, so a
# fragment's own `try { for(;;){} } catch(e){}` cannot swallow it.  A fragment
# lowered to native C by maxim (COMCON C5 server-AOT, the tier tenants actually
# run on in a process that has a compiler) has its OWN catch dispatch in the
# generated code -- and it did not ask.  Measured, before the fix: the fragment
# below returned "SURVIVED the interrupt" on objs_jit and was stopped on the
# interpreter.  A hostile version (`for(;;){ try { for(;;){} } catch(e){} }`)
# loops forever: each interrupt is caught, the deadline stays passed, the next
# poll throws again.
#
# Found by the authoring tier's basic test, whose parent fragment caught its
# sub-fragment's abort on the JIT build only.  This file is the direct probe;
# it runs on BOTH builds and expects the same answer from each, and it reports
# whether the fragment was in fact compiled (aotStatus) so a pass on a build
# with no compiler is not mistaken for a pass of the compiled tier (F5).
#
# The hostile loop is LAST and bounded by the harness alarm: with the fix it
# ends at the deadline; without it the request hangs and the alarm fails the
# test, which is the honest outcome for an infinite loop.

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

        location /catch { }
        location /hostile { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
nginx.workerRequestTimeout = 300;

/* catches once and returns: the polite version, which SAYS whether the
   handler ran */
var polite = comcon.include(
    "function(req){" +
    "  var n = 0;" +
    "  for (;;) {" +
    "    try { for (;;) { n = (n + 1) % 1000000; } }" +
    "    catch (e) { return { status: 200, body: 'SURVIVED the interrupt: ' + String(e && e.message || e) }; }" +
    "  }" +
    "}", {imports: ['String']});

/* catches forever: the hostile version */
var hostile = comcon.include(
    "function(req){" +
    "  var n = 0;" +
    "  for (;;) { try { for (;;) { n = (n + 1) % 1000000; } } catch (e) { n = 0; } }" +
    "}", {imports: []});

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    (function (path) {
        var f = { '/catch': polite, '/hostile': hostile }[path];
        if (!f) { return; }
        locs[i].handler = function (req) {
            var st = comcon.__aotStatus(f.handle);
            var t0 = Date.now();
            try {
                var o = f({});
                req.respond(200, {'content-type': 'text/plain'},
                            'MISSED ' + (Date.now() - t0) + 'ms compiled=' + st.compiled + ' ' + o.body);
            } catch (e) {
                req.respond(200, {'content-type': 'text/plain'},
                            'FIRED ' + (Date.now() - t0) + 'ms compiled=' + st.compiled + ' '
                            + String(e && e.message || e));
            }
        };
    })(locs[i].path);
}
JS

$t->try_run('no js module')->plan(5);

my $r = http_get('/catch');
$r =~ s/^.*?\r\n\r\n//s;
diag("catch: $r");
like($r, qr/^FIRED /, 'a fragment cannot catch its own deadline interrupt');
unlike($r, qr/SURVIVED/, '... its catch handler did not run');
my ($ms) = $r =~ /^FIRED (\d+)ms/;
cmp_ok($ms // 0, '>=', 200, '... and it ran until the deadline, not an unrelated throw');
my ($compiled) = $r =~ /compiled=(\w+)/;
diag("this build ran the fragment " . (($compiled // '0') =~ /^(1|true)$/
                                        ? 'COMPILED (the tier F16 is about)'
                                        : 'interpreted (the control)'));

my $h = http_get('/hostile');
$h =~ s/^.*?\r\n\r\n//s;
diag("hostile: $h");
like($h, qr/^FIRED /, 'a fragment that catches forever is still stopped at the deadline');
my ($hms) = $h =~ /^FIRED (\d+)ms/;
cmp_ok($hms // 9999, '<', 2000, '... promptly: the first interrupt ended it');
