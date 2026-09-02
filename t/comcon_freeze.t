#!/usr/bin/perl

# COMCON M-SES-1: intrinsic freezing (prototype-pollution isolation).
#
# The tenant runtime is long-lived (created once, serves every request), so a
# mutation of a shared intrinsic — Object.prototype.x = ... — would PERSIST
# across requests (and, under multi-tenancy, across tenants). M-SES-1 freezes
# the intrinsic graph at context creation, so such a write throws (strict) and
# never takes effect. Normal JS is unaffected: only MUTATING intrinsics is
# refused; creating and using instances still works.

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

js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /t { js_tenant_handler; }
    }
}
EOF

# Probe: is Object.prototype frozen? does a pollution write throw? does anything
# leak across requests (evil present before we try to set it)?
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    var before = ("evil" in Object.prototype);
    var frozen = Object.isFrozen(Object.prototype)
              && Object.isFrozen(Array.prototype)
              && Object.isFrozen(String.prototype);
    // SR-3: shared iterator instance-prototypes reachable only by calling a
    // method (string/map/set) must be frozen too, else a tenant pollutes them
    // across requests.
    var iters = Object.isFrozen(Object.getPrototypeOf(""[Symbol.iterator]()))
             && Object.isFrozen(Object.getPrototypeOf(new Map()[Symbol.iterator]()))
             && Object.isFrozen(Object.getPrototypeOf(new Set()[Symbol.iterator]()));
    var itersPoison = ("evil" in Object.getPrototypeOf(""[Symbol.iterator]()));
    try { Object.getPrototypeOf(""[Symbol.iterator]()).evil = "PWNED"; } catch (e) {}
    var threw = false;
    try { Object.prototype.evil = "PWNED"; } catch (e) { threw = true; }
    var o = {}; o.x = 1;                       // own-object mutation is fine
    var a = []; a.push("p"); a.push("q");      // own-array mutation is fine
    return "before=" + before + " frozen=" + frozen + " threw=" + threw
         + " iters=" + iters + " itersPoison=" + itersPoison
         + " own=" + o.x + a.join("") + "\n";
});
JS

$t->try_run('no js module')->plan(9);

# --- intrinsics are frozen, the pollution write throws ---
my $r1 = http_get('/t');
like($r1, qr/frozen=true/,  'Object/Array/String prototypes are frozen');
like($r1, qr/threw=true/,   'a prototype-pollution write throws (frozen intrinsic)');
like($r1, qr/before=false/, 'first request sees a pristine Object.prototype');

# --- SR-3: sibling iterator instance-prototypes are frozen too ---
like($r1, qr/iters=true/,        'string/map/set iterator prototypes are frozen (SR-3)');
like($r1, qr/itersPoison=false/, 'first request sees a pristine string-iterator prototype');

# --- a tenant can still create and mutate its OWN objects/arrays ---
like($r1, qr/own=1pq/, 'freezing intrinsics does not block own-object/array mutation');

# --- no persistence: the failed write did not leak into a later request ---
my $r2 = http_get('/t');
like($r2, qr/before=false/, 'no cross-request pollution: still pristine on req 2');
like($r2, qr/itersPoison=false/,
    'no cross-request pollution of the iterator prototype either (SR-3)');

my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

sub try_tenant {
    my ($tag, $body) = @_;
    open my $j, '>', "$dir/$tag.js" or die;
    print $j $body;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die;
    print $c "daemon off;\npid $dir/$tag.pid;\nerror_log $dir/$tag.log;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8096; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- freezing does not break ordinary tenant JS ---
like(try_tenant('ok_legit',
    "onRequest(function(req){ var a=[3,1,2].map(function(x){return x*2;}).sort();"
  . " return JSON.stringify({s:a})+' '+'ab'.toUpperCase()+' '+Math.max(1,9)+String.fromCharCode(65); });\n"),
    qr/test is successful/,
    'ordinary Array/Object/String/Math/JSON tenant code still admitted');
