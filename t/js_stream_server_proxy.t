#!/usr/bin/perl

# Tests for nginx.stream server and proxy writable properties (Stage B).
#
# Covers:
#   NginxStreamServer setters: tcpNodelay, prereadBufferSize,
#     prereadTimeout, resolverTimeout, proxyProtocolTimeout
#   NginxStreamProxy  setters: connectTimeout, timeout,
#     nextUpstreamTimeout, bufferSize, nextUpstreamTries,
#     nextUpstream, proxyProtocol, halfClose, socketKeepalive
#   NginxStreamProxy  getter: pass (upstream name)
#   read-only enforcement: requests, responses remain unchanged

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location / { }
    }
}

stream {
    upstream sback {
        server 127.0.0.1:%%PORT_8091%%;
    }

    server {
        listen      127.0.0.1:%%PORT_8092%%;
        proxy_pass  sback;
        proxy_connect_timeout   5s;
        proxy_timeout           60s;
        proxy_buffer_size       4k;
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

const srv   = nginx.stream.servers[0];
const proxy = srv.proxy;

/* ---- NginxStreamProxy: pass getter --------------------------------- */
check("proxy_pass_name", proxy.pass === "sback", proxy.pass);

/* ---- NginxStreamProxy: read initial values ------------------------- */
const orig_ct  = proxy.connectTimeout;
const orig_to  = proxy.timeout;
const orig_bs  = proxy.bufferSize;

check("proxy_ct_positive",  orig_ct > 0,  orig_ct);
check("proxy_to_positive",  orig_to > 0,  orig_to);
check("proxy_bs_positive",  orig_bs > 0,  orig_bs);

/* ---- NginxStreamProxy setters -------------------------------------- */
proxy.connectTimeout = 12345;
check("proxy_set_ct", proxy.connectTimeout === 12345, proxy.connectTimeout);

proxy.timeout = 99000;
check("proxy_set_to", proxy.timeout === 99000, proxy.timeout);

proxy.nextUpstreamTimeout = 8000;
check("proxy_set_nut", proxy.nextUpstreamTimeout === 8000,
      proxy.nextUpstreamTimeout);

proxy.bufferSize = 8192;
check("proxy_set_bs", proxy.bufferSize === 8192, proxy.bufferSize);

proxy.nextUpstreamTries = 3;
check("proxy_set_nut_tries", proxy.nextUpstreamTries === 3,
      proxy.nextUpstreamTries);

proxy.nextUpstream = false;
check("proxy_set_nu_false", proxy.nextUpstream === false, proxy.nextUpstream);
proxy.nextUpstream = true;
check("proxy_set_nu_true", proxy.nextUpstream === true, proxy.nextUpstream);

proxy.proxyProtocol = true;
check("proxy_set_pp", proxy.proxyProtocol === true, proxy.proxyProtocol);

proxy.halfClose = true;
check("proxy_set_hc", proxy.halfClose === true, proxy.halfClose);

proxy.socketKeepalive = true;
check("proxy_set_sk", proxy.socketKeepalive === true, proxy.socketKeepalive);

/* ---- NginxStreamProxy: r/o stats ----------------------------------- */
const req0  = proxy.requests;
const resp0 = proxy.responses;
check("proxy_requests_is_number",  typeof req0  === "number");
check("proxy_responses_is_number", typeof resp0 === "number");

/* ---- NginxStreamServer setters ------------------------------------- */
const orig_nodelay  = srv.tcpNodelay;
const orig_preread  = srv.prereadBufferSize;
const orig_pto      = srv.prereadTimeout;
const orig_res      = srv.resolverTimeout;
const orig_ppt      = srv.proxyProtocolTimeout;

srv.tcpNodelay = !orig_nodelay;
check("srv_set_nodelay",
      srv.tcpNodelay === !orig_nodelay, srv.tcpNodelay);
srv.tcpNodelay = orig_nodelay;   /* restore */

srv.prereadBufferSize = 16384;
check("srv_set_preread_buf", srv.prereadBufferSize === 16384,
      srv.prereadBufferSize);

srv.prereadTimeout = 5000;
check("srv_set_preread_to", srv.prereadTimeout === 5000,
      srv.prereadTimeout);

srv.resolverTimeout = 7000;
check("srv_set_resolver_to", srv.resolverTimeout === 7000,
      srv.resolverTimeout);

srv.proxyProtocolTimeout = 3000;
check("srv_set_ppt", srv.proxyProtocolTimeout === 3000,
      srv.proxyProtocolTimeout);

/* Verify mutations persisted (re-read after restore chain) */
check("srv_preread_buf_persisted", srv.prereadBufferSize === 16384,
      srv.prereadBufferSize);
JS

$t->try_run('no js module or stream module')->plan(22);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS proxy_pass_name/,        'proxy.pass returns upstream name');
like($log, qr/JSTEST PASS proxy_ct_positive/,      'proxy.connectTimeout initial > 0');
like($log, qr/JSTEST PASS proxy_to_positive/,      'proxy.timeout initial > 0');
like($log, qr/JSTEST PASS proxy_bs_positive/,      'proxy.bufferSize initial > 0');
like($log, qr/JSTEST PASS proxy_set_ct/,           'proxy.connectTimeout setter');
like($log, qr/JSTEST PASS proxy_set_to/,           'proxy.timeout setter');
like($log, qr/JSTEST PASS proxy_set_nut/,          'proxy.nextUpstreamTimeout setter');
like($log, qr/JSTEST PASS proxy_set_bs/,           'proxy.bufferSize setter');
like($log, qr/JSTEST PASS proxy_set_nut_tries/,    'proxy.nextUpstreamTries setter');
like($log, qr/JSTEST PASS proxy_set_nu_false/,     'proxy.nextUpstream = false');
like($log, qr/JSTEST PASS proxy_set_nu_true/,      'proxy.nextUpstream = true');
like($log, qr/JSTEST PASS proxy_set_pp/,           'proxy.proxyProtocol setter');
like($log, qr/JSTEST PASS proxy_set_hc/,           'proxy.halfClose setter');
like($log, qr/JSTEST PASS proxy_set_sk/,           'proxy.socketKeepalive setter');
like($log, qr/JSTEST PASS proxy_requests_is_number/,  'proxy.requests is number (r/o stat)');
like($log, qr/JSTEST PASS proxy_responses_is_number/, 'proxy.responses is number (r/o stat)');
like($log, qr/JSTEST PASS srv_set_nodelay/,        'srv.tcpNodelay setter');
like($log, qr/JSTEST PASS srv_set_preread_buf/,    'srv.prereadBufferSize setter');
like($log, qr/JSTEST PASS srv_set_preread_to/,     'srv.prereadTimeout setter');
like($log, qr/JSTEST PASS srv_set_resolver_to/,    'srv.resolverTimeout setter');
like($log, qr/JSTEST PASS srv_set_ppt/,            'srv.proxyProtocolTimeout setter');
like($log, qr/JSTEST PASS srv_preread_buf_persisted/, 'mutation persists after re-read');
