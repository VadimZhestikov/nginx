#!/usr/bin/perl

# COMCON increment C (C4): the fragment artifact ("fat bytecode"). After the C3
# admission checks pass, the admitted fragment gets a content-addressed identity
# and an admission certificate:
#
#   content hash = SHA-256 over the tenant sources
#   identity     = SHA-256(content_hash ‖ schema-version)   (SPEC §8)
#   certificate  = which admission checks cleared (free-names, no dynamic code,
#                  Request sealed, onRequest signature) + the env-signature size
#
# The identity folds the content pin and the schema pin into one match:
# js_tenant_artifact <hex> refuses the config on EITHER content drift (the
# fragment changed) or schema drift (the C2 surface changed). This generalizes
# B/E1's pin-by-hash from a dependency file to the whole fragment. No lowering
# here — the artifact is the T1-executable record C5 will lower.

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

my $frag = "onRequest(function(req) { return req.method + \" \" + req.uri + \"\\n\"; });\n";

# identity = H(H(source) ‖ schema-version) — the same formula the module computes.
my $ident = sha256_hex(sha256($frag) . 'c2-tenant-env-1');

$t->write_file('tenant.js', $frag);

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_tenant_source %%TESTDIR%%/tenant.js;
js_tenant_artifact $ident;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /t { js_tenant_handler; }
    }
}
EOF

$t->try_run('no js module')->plan(6);

# --- the correct identity pin admits, and the fragment serves ---
like(http_get('/t'), qr/GET \/t/, 'a correctly-pinned artifact is admitted and serves');

# --- the admission certificate is logged at load ---
like($t->read_file('error.log'),
     qr/fragment artifact [0-9a-f]{16} admitted \(schema c2-tenant-env-1;/,
     'the artifact identity + schema version are recorded at admission');
like($t->read_file('error.log'),
     qr/cert free-names=1 dyn-code-free=1 request-sealed=1 onRequest-sig=1/,
     'the admission certificate records every C3 clearance');

# --- the logged short identity is the first 16 hex chars of the full identity ---
like($t->read_file('error.log'), qr/fragment artifact \Q${\ substr($ident,0,16) }\E /,
     'the logged short id is the prefix of H(content ‖ schema-version)');

my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

sub try_pin {
    my ($tag, $src, $pin) = @_;
    open my $j, '>', "$dir/$tag.js" or die;
    print $j $src;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die;
    print $c "daemon off;\npid $dir/$tag.pid;\nerror_log $dir/$tag.log;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "js_tenant_artifact $pin;\n" if $pin;
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8093; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- a wrong pin is refused (content or schema drift) ---
my $wrong = '00' . substr($ident, 2);
my $o1 = try_pin('badpin', $frag, $wrong);
like($o1, qr/artifact identity mismatch/,
     'a mismatched identity pin refuses the config (COMCON C4 pin)');

# --- content drift: the SAME correct-for-original pin now mismatches a changed
#     fragment (one extra space changes the content hash → the identity) ---
my $drifted = "onRequest(function(req) { return req.method + \"  \" + req.uri + \"\\n\"; });\n";
my $o2 = try_pin('drift', $drifted, $ident);
like($o2, qr/artifact identity mismatch/,
     'content drift: a byte-changed fragment fails the original identity pin');
