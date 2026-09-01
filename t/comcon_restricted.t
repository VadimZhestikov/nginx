#!/usr/bin/perl

# COMCON increment C (C3-rest): the restricted-construct admission check.
# C3.0's static free-name analysis is only SOUND if a fragment cannot conjure
# name references the analysis can't see. Dynamic code does exactly that: a host
# name can hide inside an `eval`/`Function` string, and `with` splices an object
# into the scope chain. So the restricted profile REFUSES a fragment at LOAD if
# it (a) references `eval`/`Function` — standard globals, present on the tenant
# global, so the free-name gate would otherwise wave them through — or (b) uses
# direct eval / `with` in its bytecode. A clean fragment is still admitted.

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

# Clean: no dynamic code, only report + standard JS. Admitted.
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    return "ok " + JSON.stringify({a: 1}).length + " " + req.uri + "\n";
});
JS

$t->try_run('no js module')->plan(7);

# --- clean fragment still serves (baseline: the gate does not over-reject) ---
like(http_get('/t'), qr/ok \d+ \/t/, 'clean fragment admitted and serves');

my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

# helper: write a tenant + minimal conf and return `nginx -t` output
sub try_tenant {
    my ($tag, $body) = @_;
    open my $j, '>', "$dir/$tag.js" or die;
    print $j $body;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die;
    print $c "daemon off;\npid $dir/$tag.pid;\nerror_log $dir/$tag.log;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8091; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- reject: direct eval (caught by the bytecode scan, OP_eval) ---
my $o1 = try_tenant('bad_eval',
    "onRequest(function(req){ return eval('1+1'); });\n");
like($o1, qr/dynamic code|dynamic-code/,
     'reject: direct eval refused as dynamic code');
like($o1, qr/COMCON C3/, 'refusal cites the C3 gate');

# --- reject: eval referenced indirectly (caught by the name deny-list) ---
my $o2 = try_tenant('bad_eval_ref',
    "onRequest(function(req){ var f = eval; return f('1'); });\n");
like($o2, qr/dynamic-code name "eval"/,
     'reject: indirect eval reference refused by name deny-list');

# --- reject: the Function constructor (name deny-list) ---
my $o3 = try_tenant('bad_function',
    "onRequest(function(req){ return (new Function('return 1'))(); });\n");
like($o3, qr/dynamic-code name "Function"/,
     'reject: Function constructor refused by name deny-list');

# --- admit: a fragment that merely NAMES a local `eval`-ish variable is fine ---
# (no reference to the global eval/Function, no dynamic opcodes)
my $o4 = try_tenant('ok_clean',
    "onRequest(function(req){ var evaluate = req.uri.length; return String(evaluate); });\n");
like($o4, qr/test is successful/,
     'admit: a local name that merely contains "eval" is not the global eval');

# --- admit: nested plain functions (no dynamic code) still pass ---
my $o5 = try_tenant('ok_nested',
    "onRequest(function(req){ function d(n){ return n+n; } return String(d(req.uri.length)); });\n");
like($o5, qr/test is successful/,
     'admit: nested plain function without dynamic code is admitted');
