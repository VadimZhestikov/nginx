#!/usr/bin/perl

# Tests for Stage 2 COM expansion: read-only ngx_http_core_srv_conf_t fields
# exposed on NginxServer objects.
#
# New properties (all read-only):
#   clientHeaderBufferSize   number (bytes)
#   largeClientHeaderBuffers {num, size}
#   clientHeaderTimeout      number (milliseconds)
#   ignoreInvalidHeaders     bool
#   mergeSlashes             bool
#   underscoresInHeaders     bool
#   serverTokens             "off" | "on" | "build"

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

js_include %%TESTDIR%%/init_srv_conf.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    # server with explicit values for every field we expose
    server {
        listen       127.0.0.1:8080;
        server_name  example.com;

        client_header_buffer_size       2k;
        large_client_header_buffers     8 16k;
        client_header_timeout           15s;
        ignore_invalid_headers          off;
        merge_slashes                   off;
        underscores_in_headers          on;
        server_tokens                   off;

        location / { }
    }

    # second server with default / different values
    server {
        listen       127.0.0.1:8081;
        server_name  other.example.com;

        server_tokens  on;
        merge_slashes  on;

        location / { }
    }
}
EOF

$t->write_file('init_srv_conf.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const s0 = nginx.http.servers[0];   /* example.com — explicit values */
const s1 = nginx.http.servers[1];   /* other.example.com — different values */

// ---- clientHeaderBufferSize ----
check("hdr_buf_size_2k",  s0.clientHeaderBufferSize === 2048, s0.clientHeaderBufferSize);

// ---- largeClientHeaderBuffers ----
const lchb = s0.largeClientHeaderBuffers;
check("lchb_is_obj",   typeof lchb === "object" && lchb !== null, typeof lchb);
check("lchb_num_8",    lchb.num  === 8,     lchb.num);
check("lchb_size_16k", lchb.size === 16384, lchb.size);

// ---- clientHeaderTimeout ----
check("hdr_timeout_15s", s0.clientHeaderTimeout === 15000, s0.clientHeaderTimeout);

// ---- bool flags ----
check("ignore_invalid_off",    s0.ignoreInvalidHeaders  === false, s0.ignoreInvalidHeaders);
check("merge_slashes_off",     s0.mergeSlashes          === false, s0.mergeSlashes);
check("underscores_in_hdrs_on",s0.underscoresInHeaders  === true,  s0.underscoresInHeaders);

// ---- serverTokens ----
check("server_tokens_off",  s0.serverTokens === "off", s0.serverTokens);
check("server_tokens_on",   s1.serverTokens === "on",  s1.serverTokens);

// ---- s1 mergeSlashes default (on) ----
check("merge_slashes_on",   s1.mergeSlashes === true, s1.mergeSlashes);
JS

$t->try_run('no js module')->plan(11);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS hdr_buf_size_2k/,    'server.clientHeaderBufferSize == 2048');
like($log, qr/JSTEST PASS lchb_is_obj/,        'server.largeClientHeaderBuffers is object');
like($log, qr/JSTEST PASS lchb_num_8/,         'server.largeClientHeaderBuffers.num == 8');
like($log, qr/JSTEST PASS lchb_size_16k/,      'server.largeClientHeaderBuffers.size == 16384');
like($log, qr/JSTEST PASS hdr_timeout_15s/,    'server.clientHeaderTimeout == 15000');
like($log, qr/JSTEST PASS ignore_invalid_off/,  'server.ignoreInvalidHeaders == false');
like($log, qr/JSTEST PASS merge_slashes_off/,   'server.mergeSlashes == false');
like($log, qr/JSTEST PASS underscores_in_hdrs_on/, 'server.underscoresInHeaders == true');
like($log, qr/JSTEST PASS server_tokens_off/,  'server.serverTokens == "off"');
like($log, qr/JSTEST PASS server_tokens_on/,   'server.serverTokens == "on"');
like($log, qr/JSTEST PASS merge_slashes_on/,   'server.mergeSlashes == true (second server)');
