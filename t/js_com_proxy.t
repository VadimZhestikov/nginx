#!/usr/bin/perl

# Tests for Stage 4 COM expansion: ngx_http_proxy_loc_conf_t fields
# exposed as properties of the location.proxy object (NginxProxy class).
#
# New property on NginxLocation:
#   proxy   NginxProxy | null (null only if proxy module has no loc_conf)
#
# NginxProxy properties (all read-only):
#   pass                string | "dynamic" | null
#   httpVersion         "1.0" | "1.1"
#   connectTimeout      number (ms)
#   sendTimeout         number (ms)
#   readTimeout         number (ms)
#   buffering           bool
#   requestBuffering    bool
#   interceptErrors     bool
#   bufferSize          number (bytes)
#   buffers             {num, size}
#   nextUpstream        string[]
#   nextUpstreamTries   number
#   nextUpstreamTimeout number (ms)
#   setHeader           [{key, value}]

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

js_include %%TESTDIR%%/init_proxy.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # location with explicit proxy_pass and all tunable fields
        location /api/ {
            proxy_pass              http://127.0.0.1:9090/;
            proxy_http_version      1.1;
            proxy_connect_timeout   5s;
            proxy_send_timeout      10s;
            proxy_read_timeout      15s;
            proxy_buffering         off;
            proxy_request_buffering off;
            proxy_buffer_size       8k;
            proxy_buffers           4 32k;
            proxy_intercept_errors  on;
            proxy_next_upstream     error timeout http_503;
            proxy_next_upstream_tries   3;
            proxy_next_upstream_timeout 30s;
            proxy_set_header        X-Real-IP $remote_addr;
            proxy_set_header        X-Forwarded-For $proxy_add_x_forwarded_for;
        }

        # location without proxy_pass
        location /plain/ { }
    }
}
EOF

$t->write_file('init_proxy.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const locs = nginx.http.servers[0].locations;
const api   = locs.find(function(l) { return l.path === "/api/"; });
const plain = locs.find(function(l) { return l.path === "/plain/"; });

const p = api.proxy;

// ---- proxy object ----
check("proxy_obj", typeof p === "object" && p !== null, typeof p);

// ---- pass ----
check("pass_set",  typeof p.pass === "string" && p.pass.length > 0, p.pass);
check("pass_url",  p.pass.indexOf("127.0.0.1:9090") !== -1, p.pass);

// ---- httpVersion ----
check("http_ver",  p.httpVersion === "1.1", p.httpVersion);

// ---- timeouts ----
check("conn_to",   p.connectTimeout === 5000,  p.connectTimeout);
check("send_to",   p.sendTimeout    === 10000, p.sendTimeout);
check("read_to",   p.readTimeout    === 15000, p.readTimeout);

// ---- buffering flags ----
check("buffering_off",   p.buffering        === false, p.buffering);
check("req_buf_off",     p.requestBuffering === false, p.requestBuffering);

// ---- bufferSize ----
check("bufsz",     p.bufferSize === 8192, p.bufferSize);

// ---- buffers object ----
const bufs = p.buffers;
check("bufs_obj",  typeof bufs === "object" && bufs !== null, typeof bufs);
check("bufs_num",  bufs.num  === 4,     bufs.num);
check("bufs_sz",   bufs.size === 32768, bufs.size);

// ---- interceptErrors ----
check("interc",    p.interceptErrors === true, p.interceptErrors);

// ---- nextUpstream array ----
const nu = p.nextUpstream;
check("nu_arr",    Array.isArray(nu),                  typeof nu);
check("nu_err",    nu.indexOf("error")   !== -1,       nu);
check("nu_to",     nu.indexOf("timeout") !== -1,       nu);
check("nu_503",    nu.indexOf("http_503") !== -1,      nu);

// ---- nextUpstreamTries / nextUpstreamTimeout ----
check("nu_tries",  p.nextUpstreamTries   === 3,     p.nextUpstreamTries);
check("nu_to_ms",  p.nextUpstreamTimeout === 30000, p.nextUpstreamTimeout);

// ---- setHeader array ----
const sh = p.setHeader;
check("set_hdr_arr", Array.isArray(sh),    typeof sh);
check("set_hdr_2",   sh.length >= 2,       sh.length);

// ---- location without proxy_pass: pass === null ----
check("plain_pass_null", plain.proxy.pass === null, plain.proxy.pass);
JS

$t->try_run('no js module')->plan(22);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS proxy_obj/,      'location.proxy is object');
like($log, qr/JSTEST PASS pass_set/,       'location.proxy.pass is string');
like($log, qr/JSTEST PASS pass_url/,       'location.proxy.pass contains host:port');
like($log, qr/JSTEST PASS http_ver/,       'location.proxy.httpVersion == "1.1"');
like($log, qr/JSTEST PASS conn_to/,        'location.proxy.connectTimeout == 5000');
like($log, qr/JSTEST PASS send_to/,        'location.proxy.sendTimeout == 10000');
like($log, qr/JSTEST PASS read_to/,        'location.proxy.readTimeout == 15000');
like($log, qr/JSTEST PASS buffering_off/,  'location.proxy.buffering == false');
like($log, qr/JSTEST PASS req_buf_off/,    'location.proxy.requestBuffering == false');
like($log, qr/JSTEST PASS bufsz/,          'location.proxy.bufferSize == 8192');
like($log, qr/JSTEST PASS bufs_obj/,       'location.proxy.buffers is object');
like($log, qr/JSTEST PASS bufs_num/,       'location.proxy.buffers.num == 4');
like($log, qr/JSTEST PASS bufs_sz/,        'location.proxy.buffers.size == 32768');
like($log, qr/JSTEST PASS interc/,         'location.proxy.interceptErrors == true');
like($log, qr/JSTEST PASS nu_arr/,         'location.proxy.nextUpstream is array');
like($log, qr/JSTEST PASS nu_err/,         'location.proxy.nextUpstream includes "error"');
like($log, qr/JSTEST PASS nu_to/,          'location.proxy.nextUpstream includes "timeout"');
like($log, qr/JSTEST PASS nu_503/,         'location.proxy.nextUpstream includes "http_503"');
like($log, qr/JSTEST PASS nu_tries/,       'location.proxy.nextUpstreamTries == 3');
like($log, qr/JSTEST PASS nu_to_ms/,       'location.proxy.nextUpstreamTimeout == 30000');
like($log, qr/JSTEST PASS set_hdr_arr/,    'location.proxy.setHeader is array');
like($log, qr/JSTEST PASS set_hdr_2/,      'location.proxy.setHeader has >= 2 entries');
