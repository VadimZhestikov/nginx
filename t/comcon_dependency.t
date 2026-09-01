#!/usr/bin/perl

# COMCON increment B (E1): the dependency workflow — pin-by-hash + per-dep
# pure_library cages. A tenant dependency is a pure library evaluated in a
# bare, no-capability environment and admitted only if its bytes match the
# pinned SHA-256. Guarantees: a hijacked update (different bytes) is refused
# at load (the last good config keeps serving), and a dependency that reaches
# for host authority fails to load (it is truly pure).

use warnings;
use strict;

use Test::More;
use Digest::SHA qw/sha256_hex/;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# A pure library: a data transform, no host access. Its completion value is
# the export bound on the tenant global.
my $lib = "function greet(n){ return \"hello, \" + n + \"!\"; }\n({ greet: greet })\n";
my $lib_hash = sha256_hex($lib);

$t->write_file('greet.js', $lib);

$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    return greetlib.greet("tenant") + " nginx=" + (typeof nginx) + "\n";
});
JS

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_tenant_dependency greetlib %%TESTDIR%%/greet.js $lib_hash;
js_tenant_source     %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /t { js_tenant_handler; }
    }
}
EOF

$t->try_run('no js module')->plan(5);

# --- positive: correct pin, pure library loads and is usable ---
like(http_get('/t'), qr/hello, tenant!/,
     'pinned dependency loads and the tenant uses it');
like(http_get('/t'), qr/nginx=undefined/,
     'deny-by-default intact: dependencies do not widen the tenant');

my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

# --- refusal 1: a hijacked update (bytes changed → hash mismatches) ---
my $bad = "$dir/bad.conf";
{
    open my $fh, '>', "$dir/hijacked.js" or die;
    print $fh "function greet(n){ return \"EVIL\"; }\n({ greet: greet })\n";
    close $fh;
    open my $cf, '>', $bad or die;
    print $cf "daemon off;\npid $dir/bad.pid;\nerror_log $dir/bad.log;\n";
    print $cf "js_tenant_dependency greetlib $dir/hijacked.js $lib_hash;\n";
    print $cf "js_tenant_source $dir/tenant.js;\n";
    print $cf "events { }\nhttp { server { listen 127.0.0.1:8081; location /t { js_tenant_handler; } } }\n";
    close $cf;
}
my $out1 = `$bin -t -p $dir -c $bad 2>&1`;
isnt($?, 0, 'a hijacked dependency (hash mismatch) refuses the config');
like($out1, qr/hash mismatch/, 'refusal names the supply-chain pin');

# --- refusal 2: a "pure" library that reaches for host authority ---
my $reach = "nginx.log(6, 'x');\n({})\n";
$t->write_file('reach.js', $reach);
my $reach_hash = sha256_hex($reach);
my $bad2 = "$dir/reach.conf";
{
    open my $cf, '>', $bad2 or die;
    print $cf "daemon off;\npid $dir/r.pid;\nerror_log $dir/r.log;\n";
    print $cf "js_tenant_dependency lib $dir/reach.js $reach_hash;\n";
    print $cf "js_tenant_source $dir/tenant.js;\n";
    print $cf "events { }\nhttp { server { listen 127.0.0.1:8082; location /t { js_tenant_handler; } } }\n";
    close $cf;
}
my $out2 = `$bin -t -p $dir -c $bad2 2>&1`;
like($out2, qr/failed to load|pure library/,
     'a dependency that reaches for host authority is not admitted (pure cage)');
