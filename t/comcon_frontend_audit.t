#!/usr/bin/perl

# COMCON — front-end soundness audit regressions (2026-09-01).
#
# An adversarial audit of the increment-C admission front-end (C3.0 free-name,
# C3-rest restricted constructs, C3-types, C4 artifact) found that CONFINEMENT
# HELD against every vector — no capability escaped — but two soundness *claims*
# were overstated. This file pins the guarantees that must never regress and the
# one fix the audit landed:
#
#  * FIX: reflective global aliases (globalThis/global/self) are refused — they
#    would let `globalThis[<computed>]` reach a bound name without it appearing
#    in the static free-name manifest.
#
#  * DYNAMIC CODE CLOSED (M-SES-0): the audit found dynamic code was still
#    reachable via `[].constructor.constructor` (contained by deny-by-default,
#    but the "no dynamic code" claim was false). M-SES-0's lockdown neutralizes
#    the Function / generator / async constructors, so that route now THROWS.
#    This test pins that it is closed.

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

# The tenant tries the Function-constructor route to build dynamic code. Under
# M-SES-0 the constructor is tamed, so the attempt throws — the fragment loads
# (the call site is not a named reference), but the route is dead at runtime.
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    try {
        [].constructor.constructor("return 1")();
        return "DYNAMIC-CODE-RAN\n";
    } catch (e) {
        return "dynamic-code-blocked\n";
    }
});
JS

$t->try_run('no js module')->plan(6);

# --- M-SES-0: the Function-constructor dynamic-code route is closed ---
like(http_get('/t'), qr/dynamic-code-blocked/,
     'M-SES-0: [].constructor.constructor is tamed — dynamic code throws');

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
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8094; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- FIX: reflective global aliases are refused ---
like(try_tenant('r_globalThis', "onRequest(function(req){ return String(globalThis); });\n"),
     qr/reflective global "globalThis"/,
     'globalThis refused: it defeats the free-name manifest');
like(try_tenant('r_self', "onRequest(function(req){ return String(self); });\n"),
     qr/reflective global "self"/,
     'self refused (reflective global alias)');
like(try_tenant('r_global', "onRequest(function(req){ return String(global); });\n"),
     qr/reflective global "global"/,
     'global refused (reflective global alias)');

# --- a clean fragment is unaffected by the tightening ---
like(try_tenant('ok_clean', "onRequest(function(req){ return req.uri; });\n"),
     qr/test is successful/,
     'the reflective-global deny-list does not touch clean fragments');

# --- KNOWN GAP (documented, LOW, no security impact): a destructured Request
#     parameter bypasses the sealed-Request field check. Pinned so the gap is
#     visible and its eventual closure (C5 type-completeness) is a change here. ---
like(try_tenant('gap_destructure', "onRequest(function({ secret }){ return String(secret); });\n"),
     qr/test is successful/,
     'KNOWN GAP: destructured Request param evades the sealed-field check (C5 remainder)');
