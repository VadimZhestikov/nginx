#!/usr/bin/perl

# COMCON CONVERGE P4 / P6 lockdown-test strategy: the M-SES intrinsic-freeze
# suite of comcon_freeze.t, re-expressed on include. The lockdown is shared
# (ngx_js_tenant_lockdown runs in comcon_ctx too), so a confined include fragment
# sees the same frozen intrinsic graph: Object/Array/String prototypes and the
# call-only iterator instance-prototypes are frozen, prototype-pollution writes
# throw, and own-object mutation is unaffected.

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
        location /f { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var h = comcon.include(
    "function(req){" +
    "  var before = ('evil' in Object.prototype);" +
    "  var frozen = Object.isFrozen(Object.prototype)" +
    "            && Object.isFrozen(Array.prototype)" +
    "            && Object.isFrozen(String.prototype);" +
    "  var iters = Object.isFrozen(Object.getPrototypeOf(''[Symbol.iterator]()))" +
    "           && Object.isFrozen(Object.getPrototypeOf(new Map()[Symbol.iterator]()))" +
    "           && Object.isFrozen(Object.getPrototypeOf(new Set()[Symbol.iterator]()));" +
    "  var itersPoison = ('evil' in Object.getPrototypeOf(''[Symbol.iterator]()));" +
    "  try { Object.getPrototypeOf(''[Symbol.iterator]()).evil = 'PWNED'; } catch(e){}" +
    "  var threw = false;" +
    "  try { Object.prototype.evil = 'PWNED'; } catch(e){ threw = true; }" +
    "  var o = {}; o.x = 1; var a = []; a.push('p'); a.push('q');" +
    "  return { status:200, body: 'before=' + before + ' frozen=' + frozen" +
    "         + ' threw=' + threw + ' iters=' + iters + ' itersPoison=' + itersPoison" +
    "         + ' own=' + o.x + a.join('') }; }");

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/f") {
        locs[i].handler = function(req) {
            var o = h({ method: req.method });
            req.respond(o.status, {'content-type':'text/plain'}, o.body);
        };
    }
}
JS

$t->try_run('no js module')->plan(6);

my $r1 = http_get('/f');
like($r1, qr/frozen=true/,  'Object/Array/String prototypes are frozen in the confined fragment');
like($r1, qr/threw=true/,   'a prototype-pollution write throws (frozen intrinsic)');
like($r1, qr/before=false/, 'the fragment sees a pristine Object.prototype (no pollution)');
like($r1, qr/iters=true/,   'the call-only iterator instance-prototypes are frozen (SR-3)');
like($r1, qr/itersPoison=false/, 'the iterator prototypes stay unpolluted');
like($r1, qr/own=1pq/,      'own-object / own-array mutation is unaffected by the freeze');
