#!/usr/bin/perl

# COMCON M-SES-0: dynamic-code lockdown (curated intrinsics + SES-style taming).
#
# The tenant context is built with JS_NewContextRaw + a curated intrinsic set
# (Proxy omitted) and then locked down: the Function / generator / async /
# async-generator constructors are neutralized (`.constructor.constructor`
# throws), and the `eval`/`Function`/`Reflect` globals are removed. This makes
# C3-rest's "no dynamic code" guarantee sound (front-end audit finding A1) — a
# hard prerequisite for C5 erasure soundness. Standard JS is unaffected.

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

# Exercise every dynamic-code constructor route + confirm the full curated
# intrinsic set is usable. Each route is wrapped so the handler still returns.
$t->write_file('tenant.js', <<'JS');
function closed(fn) { try { fn(); return "open"; } catch (e) { return "closed"; } }

onRequest(function(req) {
    var r = [
        closed(function(){ return [].constructor.constructor("return 1")(); }),
        closed(function(){ return Object.constructor("return 1")(); }),
        closed(function(){ return Object.getPrototypeOf(function*(){}).constructor("x"); }),
        closed(function(){ return Object.getPrototypeOf(async function(){}).constructor("x"); })
    ].join(",");

    // curated standard JS must all work
    var std = [
        JSON.stringify({a:[1,2].map(function(x){return x*2;})}),
        String(Math.max(3,4)),
        String(/ab/.test("zaby")),
        String(new Map([["k",7]]).get("k")),
        String(new Set([1,1,2]).size),
        (typeof Promise),
        (typeof new Uint8Array(2).length),
        (typeof new Date().getTime())
    ].join(" ");

    return "routes=" + r + " | std=" + std + "\n";
});
JS

$t->try_run('no js module')->plan(6);

my $body = http_get('/t');

# --- every dynamic-code constructor route is closed ---
like($body, qr/routes=closed,closed,closed,closed/,
     'all four evaluator-constructor routes throw (Function/Object/generator/async)');

# --- the curated standard-JS surface is intact ---
like($body, qr{std=\{"a":\[2,4\]\} 4 true 7 2 function number number},
     'curated intrinsics work: JSON/Math/RegExp/Map/Set/Promise/TypedArray/Date');

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
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8095; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- the removed reflective globals are no longer nameable (refused at load) ---
like(try_tenant('r_Function', "onRequest(function(req){ return String(Function); });\n"),
     qr/dynamic-code name "Function"/,
     'Function is refused (dynamic-code name deny-list)');
like(try_tenant('r_Reflect', "onRequest(function(req){ return Reflect.construct(Object,[]); });\n"),
     qr/ungranted name "Reflect"/,
     'Reflect is deleted from the context — now an ungranted name');
like(try_tenant('r_Proxy', "onRequest(function(req){ return new Proxy({},{}); });\n"),
     qr/ungranted name "Proxy"/,
     'Proxy intrinsic omitted — now an ungranted name');

# --- a clean fragment is admitted and serves ---
like(try_tenant('ok_clean', "onRequest(function(req){ return req.uri; });\n"),
     qr/test is successful/,
     'a clean fragment is unaffected by the lockdown');
