#!/usr/bin/perl

# Tests for Stage 3 COM expansion: read-only ngx_http_core_main_conf_t fields
# exposed as properties of the nginx.http object.
#
# New properties on nginx.http (all read-only scalars):
#   serverNamesHashMaxSize      number
#   serverNamesHashBucketSize   number
#   variablesHashMaxSize        number
#   variablesHashBucketSize     number
#
# These directives are only settable at the http{} level and have no
# per-server or per-location counterpart.

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

js_source %%TESTDIR%%/init_http_main.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server_names_hash_max_size      1024;
    server_names_hash_bucket_size   128;
    variables_hash_max_size         2048;
    variables_hash_bucket_size      256;

    server {
        listen      127.0.0.1:8080;
        server_name localhost;
        location /  { }
    }
}
EOF

$t->write_file('init_http_main.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const h = nginx.http;

check("snhms_1024",  h.serverNamesHashMaxSize    === 1024, h.serverNamesHashMaxSize);
check("snhbs_128",   h.serverNamesHashBucketSize === 128,  h.serverNamesHashBucketSize);
check("vhms_2048",   h.variablesHashMaxSize      === 2048, h.variablesHashMaxSize);
check("vhbs_256",    h.variablesHashBucketSize   === 256,  h.variablesHashBucketSize);
JS

$t->try_run('no js module')->plan(4);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS snhms_1024/, 'nginx.http.serverNamesHashMaxSize == 1024');
like($log, qr/JSTEST PASS snhbs_128/,  'nginx.http.serverNamesHashBucketSize == 128');
like($log, qr/JSTEST PASS vhms_2048/,  'nginx.http.variablesHashMaxSize == 2048');
like($log, qr/JSTEST PASS vhbs_256/,   'nginx.http.variablesHashBucketSize == 256');
