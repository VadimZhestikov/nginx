#!/usr/bin/perl

# COMCON step-4 (directive retirement): comcon.dependency(name, path, sha256hex)
# replaces js_tenant_dependency — a pinned pure-library dependency registered
# from the js_source root script (no directive). Same pin-by-hash guarantee:
# the library is admitted only if its bytes match the SHA-256, bound on the
# tenant global as `name`; a hijacked update (hash mismatch) refuses the config.

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

my $lib = "function greet(n){ return \"hello, \" + n + \"!\"; }\n({ greet: greet })\n";
my $lib_hash = sha256_hex($lib);

$t->write_file('greet.js', $lib);

# The tenant uses the pinned dependency at eval time (report → error.log),
# so this parity test needs neither onRequest nor js_tenant_handler.
$t->write_file('tenant.js', <<'JS');
report("greet=" + greetlib.greet("tenant"));
JS

# Only js_source — the dependency + tenant are registered via operators.
$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location / { }
    }
}
EOF

$t->write_file_expand('root.js', <<"JS");
comcon.dependency('greetlib', '%%TESTDIR%%/greet.js', '$lib_hash');
comcon.tenant('%%TESTDIR%%/tenant.js');
JS

$t->try_run('no js module')->plan(2);

my $log = $t->read_file('error.log');

like($log, qr/js tenant: greet=hello, tenant!/,
     'comcon.dependency(): the pinned pure library loads, is bound, and is usable');

# --- refusal: a hijacked update (bytes changed -> hash mismatches) ---
my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

$t->write_file('hijacked.js',
    "function greet(n){ return \"EVIL\"; }\n({ greet: greet })\n");
$t->write_file('root_bad.js',
    "comcon.dependency('greetlib', '$dir/hijacked.js', '$lib_hash');\n"
    . "comcon.tenant('$dir/tenant.js');\n");

my $bad = "$dir/bad.conf";
open my $cf, '>', $bad or die;
print $cf "daemon off;\npid $dir/bad.pid;\nerror_log $dir/bad.log;\n";
print $cf "js_source $dir/root_bad.js;\n";
print $cf "events { }\nhttp { server { listen 127.0.0.1:8081; location / { } } }\n";
close $cf;

my $out = `$bin -t -p $dir -c $bad 2>&1` . `cat $dir/bad.log 2>&1`;
isnt($?, 0, 'a hijacked dependency via comcon.dependency() refuses the config');
