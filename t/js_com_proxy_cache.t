#!/usr/bin/perl

# Tests for Stage 9 COM expansion: proxy cache configuration
# exposed as properties of location.proxy.cache (NginxProxyCache class).
#
# New property on NginxProxy:
#   cache   NginxProxyCache | null (null if no proxy_cache directive)
#
# NginxProxyCache properties (all read-only):
#   zone              string    cache zone name
#   minUses           number    proxy_cache_min_uses
#   methods           string[]  cacheable HTTP methods
#   lock              bool      proxy_cache_lock
#   lockTimeout       number    proxy_cache_lock_timeout (ms)
#   lockAge           number    proxy_cache_lock_age (ms)
#   revalidate        bool      proxy_cache_revalidate
#   convertHead       bool      proxy_cache_convert_head
#   backgroundUpdate  bool      proxy_cache_background_update
#   valid             object[]  [{status, seconds}]
#   useStale          string[]  proxy_cache_use_stale flags

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy cache/);

my $d = $t->testdir();

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_proxy_cache.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    proxy_cache_path %%TESTDIR%%/cache levels=1:2
                     keys_zone=test_zone:1m inactive=10m;

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # Location with explicit cache configuration
        location /cached {
            proxy_pass                    http://127.0.0.1:8081;
            proxy_cache                   test_zone;
            proxy_cache_min_uses          3;
            proxy_cache_methods           GET HEAD POST;
            proxy_cache_lock              on;
            proxy_cache_lock_timeout      5s;
            proxy_cache_lock_age          10s;
            proxy_cache_revalidate        on;
            proxy_cache_convert_head      on;
            proxy_cache_background_update on;
            proxy_cache_valid             200 1h;
            proxy_cache_valid             404 5m;
            proxy_cache_use_stale         error timeout http_500;
        }

        # Location with no proxy_cache — cache should be null
        location /uncached {
            proxy_pass  http://127.0.0.1:8081;
        }
    }
}
EOF

mkdir("$d/cache");

$t->write_file('init_proxy_cache.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Locations alphabetically: /cached (c) before /uncached (u)
const cachedLoc   = srv.locations[0];
const uncachedLoc = srv.locations[1];

const proxy = cachedLoc.proxy;
const c = proxy.cache;

// ---- cache object ----
check("cache_obj",      typeof c === "object" && c !== null, typeof c);

// ---- zone ----
check("zone_str",       typeof c.zone === "string",          typeof c.zone);
check("zone_name",      c.zone === "test_zone",              c.zone);

// ---- minUses ----
check("min_uses",       c.minUses === 3,                     c.minUses);

// ---- methods ----
const m = c.methods;
check("methods_arr",    Array.isArray(m),                    typeof m);
check("method_get",     m.indexOf("GET")  !== -1,            m);
check("method_head",    m.indexOf("HEAD") !== -1,            m);
check("method_post",    m.indexOf("POST") !== -1,            m);

// ---- lock ----
check("lock_on",        c.lock === true,                     c.lock);
check("lock_timeout",   c.lockTimeout === 5000,              c.lockTimeout);
check("lock_age",       c.lockAge === 10000,                 c.lockAge);

// ---- boolean flags ----
check("revalidate",     c.revalidate === true,               c.revalidate);
check("convert_head",   c.convertHead === true,              c.convertHead);
check("bg_update",      c.backgroundUpdate === true,         c.backgroundUpdate);

// ---- valid ----
const v = c.valid;
check("valid_arr",      Array.isArray(v),                    typeof v);
check("valid_len",      v.length === 2,                      v.length);
check("valid0_status",  v[0].status === 200,                 v[0].status);
check("valid0_secs",    v[0].seconds === 3600,               v[0].seconds);
check("valid1_status",  v[1].status === 404,                 v[1].status);
check("valid1_secs",    v[1].seconds === 300,                v[1].seconds);

// ---- useStale ----
const us = c.useStale;
check("stale_arr",      Array.isArray(us),                   typeof us);
check("stale_error",    us.indexOf("error")   !== -1,        us);
check("stale_timeout",  us.indexOf("timeout") !== -1,        us);
check("stale_500",      us.indexOf("http_500") !== -1,       us);
check("stale_no_502",   us.indexOf("http_502") === -1,       us);

// ---- uncached location: proxy.cache is null ----
check("uncached_null",  uncachedLoc.proxy.cache === null,    typeof uncachedLoc.proxy.cache);
JS

$t->try_run('no proxy cache')->plan(26);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS cache_obj/,      'proxy.cache is object');
like($log, qr/JSTEST PASS zone_str/,       'proxy.cache.zone is string');
like($log, qr/JSTEST PASS zone_name/,      'proxy.cache.zone == "test_zone"');
like($log, qr/JSTEST PASS min_uses/,       'proxy.cache.minUses == 3');
like($log, qr/JSTEST PASS methods_arr/,    'proxy.cache.methods is array');
like($log, qr/JSTEST PASS method_get/,     'proxy.cache.methods has GET');
like($log, qr/JSTEST PASS method_head/,    'proxy.cache.methods has HEAD');
like($log, qr/JSTEST PASS method_post/,    'proxy.cache.methods has POST');
like($log, qr/JSTEST PASS lock_on/,        'proxy.cache.lock == true');
like($log, qr/JSTEST PASS lock_timeout/,   'proxy.cache.lockTimeout == 5000');
like($log, qr/JSTEST PASS lock_age/,       'proxy.cache.lockAge == 10000');
like($log, qr/JSTEST PASS revalidate/,     'proxy.cache.revalidate == true');
like($log, qr/JSTEST PASS convert_head/,   'proxy.cache.convertHead == true');
like($log, qr/JSTEST PASS bg_update/,      'proxy.cache.backgroundUpdate == true');
like($log, qr/JSTEST PASS valid_arr/,      'proxy.cache.valid is array');
like($log, qr/JSTEST PASS valid_len/,      'proxy.cache.valid.length == 2');
like($log, qr/JSTEST PASS valid0_status/,  'proxy.cache.valid[0].status == 200');
like($log, qr/JSTEST PASS valid0_secs/,    'proxy.cache.valid[0].seconds == 3600');
like($log, qr/JSTEST PASS valid1_status/,  'proxy.cache.valid[1].status == 404');
like($log, qr/JSTEST PASS valid1_secs/,    'proxy.cache.valid[1].seconds == 300');
like($log, qr/JSTEST PASS stale_arr/,      'proxy.cache.useStale is array');
like($log, qr/JSTEST PASS stale_error/,    'proxy.cache.useStale has "error"');
like($log, qr/JSTEST PASS stale_timeout/,  'proxy.cache.useStale has "timeout"');
like($log, qr/JSTEST PASS stale_500/,      'proxy.cache.useStale has "http_500"');
like($log, qr/JSTEST PASS stale_no_502/,   'proxy.cache.useStale excludes "http_502"');
like($log, qr/JSTEST PASS uncached_null/,  'uncached proxy.cache == null');
