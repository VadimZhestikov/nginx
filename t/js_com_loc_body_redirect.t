#!/usr/bin/perl

# Tests for Stage 14 COM expansion: additional ngx_http_core_loc_conf_t
# getters on NginxLocation — client-body, redirect flags, response policy,
# and misc numeric fields.
#
# New properties on NginxLocation:
#   clientBodyBufferSize     number   — bytes
#   clientBodyInFileOnly     string   — "off" | "on" | "clean"
#   clientBodyInSingleBuffer boolean
#   resetTimedoutConnection  boolean
#   absoluteRedirect         boolean
#   serverNameInRedirect     boolean
#   portInRedirect           boolean
#   msiePadding              boolean
#   ifModifiedSince          string   — "off" | "exact" | "before"
#   maxRanges                number
#   authDelay                number   — ms
#   keepaliveTime            number   — ms
#   sendLowat                number   — bytes
#   postponeOutput           number   — bytes
#   typesHashMaxSize         number

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

js_source %%TESTDIR%%/init_loc_body_redirect.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default values
        location /default {
        }

        # custom values
        location /custom {
            client_body_buffer_size      32k;
            client_body_in_file_only     clean;
            client_body_in_single_buffer on;
            reset_timedout_connection    on;
            absolute_redirect            off;
            server_name_in_redirect      on;
            port_in_redirect             off;
            msie_padding                 off;
            if_modified_since            before;
            max_ranges                   10;
            auth_delay                   500ms;
            keepalive_time               120s;
            send_lowat                   4k;
            postpone_output              512;
            types_hash_max_size          2048;
        }
    }
}
EOF

$t->write_file('init_loc_body_redirect.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
let defLoc, custLoc;
for (const loc of srv.locations) {
    if      (loc.path === "/default") defLoc  = loc;
    else if (loc.path === "/custom")  custLoc = loc;
}

// ---- default: type checks ----
check("d_cbbs_num",  typeof defLoc.clientBodyBufferSize     === "number",  defLoc.clientBodyBufferSize);
check("d_cbfo_str",  typeof defLoc.clientBodyInFileOnly     === "string",  defLoc.clientBodyInFileOnly);
check("d_cbsb_bool", typeof defLoc.clientBodyInSingleBuffer === "boolean", defLoc.clientBodyInSingleBuffer);
check("d_rtc_bool",  typeof defLoc.resetTimedoutConnection  === "boolean", defLoc.resetTimedoutConnection);
check("d_ar_bool",   typeof defLoc.absoluteRedirect         === "boolean", defLoc.absoluteRedirect);
check("d_snir_bool", typeof defLoc.serverNameInRedirect     === "boolean", defLoc.serverNameInRedirect);
check("d_pir_bool",  typeof defLoc.portInRedirect           === "boolean", defLoc.portInRedirect);
check("d_mp_bool",   typeof defLoc.msiePadding              === "boolean", defLoc.msiePadding);
check("d_ims_str",   typeof defLoc.ifModifiedSince          === "string",  defLoc.ifModifiedSince);
check("d_mr_num",    typeof defLoc.maxRanges                === "number",  defLoc.maxRanges);
check("d_ad_num",    typeof defLoc.authDelay                === "number",  defLoc.authDelay);
check("d_kt_num",    typeof defLoc.keepaliveTime            === "number",  defLoc.keepaliveTime);
check("d_sl_num",    typeof defLoc.sendLowat                === "number",  defLoc.sendLowat);
check("d_po_num",    typeof defLoc.postponeOutput           === "number",  defLoc.postponeOutput);
check("d_thms_num",  typeof defLoc.typesHashMaxSize         === "number",  defLoc.typesHashMaxSize);

// ---- custom: exact values ----
check("c_cbbs",  custLoc.clientBodyBufferSize     === 32768, custLoc.clientBodyBufferSize);
check("c_cbfo",  custLoc.clientBodyInFileOnly     === "clean", custLoc.clientBodyInFileOnly);
check("c_cbsb",  custLoc.clientBodyInSingleBuffer === true,  custLoc.clientBodyInSingleBuffer);
check("c_rtc",   custLoc.resetTimedoutConnection  === true,  custLoc.resetTimedoutConnection);
check("c_ar",    custLoc.absoluteRedirect         === false, custLoc.absoluteRedirect);
check("c_snir",  custLoc.serverNameInRedirect     === true,  custLoc.serverNameInRedirect);
check("c_pir",   custLoc.portInRedirect           === false, custLoc.portInRedirect);
check("c_mp",    custLoc.msiePadding              === false, custLoc.msiePadding);
check("c_ims",   custLoc.ifModifiedSince          === "before", custLoc.ifModifiedSince);
check("c_mr",    custLoc.maxRanges                === 10,    custLoc.maxRanges);
check("c_ad",    custLoc.authDelay                === 500,   custLoc.authDelay);
check("c_kt",    custLoc.keepaliveTime            === 120000, custLoc.keepaliveTime);
check("c_sl",    typeof custLoc.sendLowat === "number",     custLoc.sendLowat);
check("c_po",    custLoc.postponeOutput           === 512,   custLoc.postponeOutput);
check("c_thms",  custLoc.typesHashMaxSize         === 2048,  custLoc.typesHashMaxSize);
JS

$t->try_run('no http module')->plan(30);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS d_cbbs_num/,  'default: clientBodyBufferSize is number');
like($log, qr/JSTEST PASS d_cbfo_str/,  'default: clientBodyInFileOnly is string');
like($log, qr/JSTEST PASS d_cbsb_bool/, 'default: clientBodyInSingleBuffer is boolean');
like($log, qr/JSTEST PASS d_rtc_bool/,  'default: resetTimedoutConnection is boolean');
like($log, qr/JSTEST PASS d_ar_bool/,   'default: absoluteRedirect is boolean');
like($log, qr/JSTEST PASS d_snir_bool/, 'default: serverNameInRedirect is boolean');
like($log, qr/JSTEST PASS d_pir_bool/,  'default: portInRedirect is boolean');
like($log, qr/JSTEST PASS d_mp_bool/,   'default: msiePadding is boolean');
like($log, qr/JSTEST PASS d_ims_str/,   'default: ifModifiedSince is string');
like($log, qr/JSTEST PASS d_mr_num/,    'default: maxRanges is number');
like($log, qr/JSTEST PASS d_ad_num/,    'default: authDelay is number');
like($log, qr/JSTEST PASS d_kt_num/,    'default: keepaliveTime is number');
like($log, qr/JSTEST PASS d_sl_num/,    'default: sendLowat is number');
like($log, qr/JSTEST PASS d_po_num/,    'default: postponeOutput is number');
like($log, qr/JSTEST PASS d_thms_num/,  'default: typesHashMaxSize is number');
like($log, qr/JSTEST PASS c_cbbs/,      'custom: clientBodyBufferSize == 32768');
like($log, qr/JSTEST PASS c_cbfo/,      'custom: clientBodyInFileOnly == "clean"');
like($log, qr/JSTEST PASS c_cbsb/,      'custom: clientBodyInSingleBuffer == true');
like($log, qr/JSTEST PASS c_rtc/,       'custom: resetTimedoutConnection == true');
like($log, qr/JSTEST PASS c_ar/,        'custom: absoluteRedirect == false');
like($log, qr/JSTEST PASS c_snir/,      'custom: serverNameInRedirect == true');
like($log, qr/JSTEST PASS c_pir/,       'custom: portInRedirect == false');
like($log, qr/JSTEST PASS c_mp/,        'custom: msiePadding == false');
like($log, qr/JSTEST PASS c_ims/,       'custom: ifModifiedSince == "before"');
like($log, qr/JSTEST PASS c_mr/,        'custom: maxRanges == 10');
like($log, qr/JSTEST PASS c_ad/,        'custom: authDelay == 500');
like($log, qr/JSTEST PASS c_kt/,        'custom: keepaliveTime == 120000');
like($log, qr/JSTEST PASS c_sl/,        'custom: sendLowat is number');
like($log, qr/JSTEST PASS c_po/,        'custom: postponeOutput == 512');
like($log, qr/JSTEST PASS c_thms/,      'custom: typesHashMaxSize == 2048');
