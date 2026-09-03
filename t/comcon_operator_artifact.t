#!/usr/bin/perl

# COMCON step-4 (directive retirement): comcon.artifact(sha256hex) replaces
# js_tenant_artifact — pin the admitted fragment's content-addressed identity
# H(H(source) ‖ schema-version) from the js_source root script (no directive).
# The tenant is admitted only if it matches; content OR schema drift refuses.

use warnings;
use strict;

use Test::More;
use Digest::SHA qw/sha256 sha256_hex/;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# A report-only tenant (admitted at eval; no per-request handler needed).
my $frag = "report(\"artifact-ok\");\n";

# identity = H(H(source) ‖ schema-version) — the same formula the module computes.
my $ident = sha256_hex(sha256($frag) . 'c2-tenant-env-1');

$t->write_file('tenant.js', $frag);

# Only js_source — the tenant + artifact pin are registered via operators.
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
comcon.tenant('%%TESTDIR%%/tenant.js');
comcon.artifact('$ident');
JS

$t->try_run('no js module')->plan(3);

my $log = $t->read_file('error.log');

like($log, qr/js tenant: artifact-ok/,
     'comcon.artifact(): a correctly-pinned tenant is admitted and runs');
like($log, qr/fragment artifact \Q${\ substr($ident,0,16) }\E admitted \(schema c2-tenant-env-1/,
     'the artifact identity + schema version are recorded at admission');

# --- refusal: a wrong pin (content or schema drift) refuses the config ---
my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

my $wrong = '00' . substr($ident, 2);
$t->write_file('root_bad.js',
    "comcon.tenant('$dir/tenant.js');\n"
    . "comcon.artifact('$wrong');\n");

open my $c, '>', "$dir/bad.conf" or die;
print $c "daemon off;\npid $dir/bad.pid;\nerror_log $dir/bad.log;\n";
print $c "js_source $dir/root_bad.js;\n";
print $c "events { }\nhttp { server { listen 127.0.0.1:8091; location / { } } }\n";
close $c;

my $out = `$bin -t -p $dir -c $dir/bad.conf 2>&1` . `cat $dir/bad.log 2>&1`;
like($out, qr/artifact identity mismatch/,
     'a mismatched identity pin refuses the config (COMCON C4 pin)');
