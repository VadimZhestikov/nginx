#!/usr/bin/perl

# Tests for Stage 1 COM expansion: read-only ngx_http_core_loc_conf_t fields
# exposed on NginxLocation objects.
#
# New properties (all read-only):
#   internal         bool
#   sendfile         bool
#   tcpNopush        bool
#   tcpNodelay       bool
#   etag             bool
#   keepaliveTimeout number (milliseconds)
#   keepaliveRequests number
#   clientMaxBodySize number (bytes)
#   clientBodyTimeout number (milliseconds)
#   sendTimeout       number (milliseconds)
#   defaultType       string
#   alias             string (alias path) | null (root directive)
#   errorPage         [{status, overwrite, uri}]

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

js_include %%TESTDIR%%/init_loc_conf.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        # location with explicit values for every field we expose
        location / {
            sendfile            on;
            tcp_nopush          on;
            tcp_nodelay         on;
            etag                on;
            keepalive_timeout   60s;
            keepalive_requests  500;
            client_max_body_size 10m;
            client_body_timeout 30s;
            send_timeout        20s;
            default_type        text/html;
            error_page 404      /not_found.html;
            error_page 500 502  /error.html;
        }

        # alias directive — alias != null, root returns the alias path
        location /static/ {
            alias %%TESTDIR%%/html/;
        }

        # internal location
        location /internal/ {
            internal;
        }
    }
}
EOF

$t->write_file('init_loc_conf.js', <<'JS');
function pass(name)       { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got)  { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv  = nginx.http.servers[0];
const locs = srv.locations;

const root = locs.find(function(l) { return l.path === "/"; });
const stat = locs.find(function(l) { return l.path === "/static/"; });
const intl = locs.find(function(l) { return l.path === "/internal/"; });

// ---- bool flags ----
check("sendfile_on",      root.sendfile   === true,  root.sendfile);
check("tcp_nopush_on",    root.tcpNopush  === true,  root.tcpNopush);
check("tcp_nodelay_on",   root.tcpNodelay === true,  root.tcpNodelay);
check("etag_on",          root.etag       === true,  root.etag);

// ---- timing (ms) ----
check("keepalive_timeout_60s",   root.keepaliveTimeout  === 60000, root.keepaliveTimeout);
check("client_body_timeout_30s", root.clientBodyTimeout === 30000, root.clientBodyTimeout);
check("send_timeout_20s",        root.sendTimeout       === 20000, root.sendTimeout);

// ---- counts / sizes ----
check("keepalive_requests_500",    root.keepaliveRequests  === 500,      root.keepaliveRequests);
check("client_max_body_10m",       root.clientMaxBodySize  === 10485760, root.clientMaxBodySize);

// ---- string ----
check("default_type_html",  root.defaultType === "text/html", root.defaultType);

// ---- errorPage array ----
const ep = root.errorPage;
check("error_page_is_array",  Array.isArray(ep),    typeof ep);
check("error_page_3_entries", ep.length === 3,       ep.length);

// first entry: 404 → /not_found.html
check("ep0_status_404",       ep[0].status === 404,             ep[0].status);
check("ep0_uri_not_found",    ep[0].uri    === "/not_found.html", ep[0].uri);

// second entry: 500 → /error.html
check("ep1_status_500",       ep[1].status === 500,          ep[1].status);
check("ep1_uri_error",        ep[1].uri    === "/error.html", ep[1].uri);

// third entry: 502 → /error.html
check("ep2_status_502",       ep[2].status === 502,          ep[2].status);
check("ep2_uri_error",        ep[2].uri    === "/error.html", ep[2].uri);

// ---- alias ----
check("root_alias_null",   root.alias === null,              root.alias);
check("static_alias_set",  typeof stat.alias === "string" && stat.alias.length > 0,
                           stat.alias);

// ---- internal ----
check("root_not_internal",     root.internal === false, root.internal);
check("internal_loc_internal", intl.internal === true,  intl.internal);
JS

$t->try_run('no js module')->plan(22);

my $log = $t->read_file('error.log');

# bool flags
like($log, qr/JSTEST PASS sendfile_on/,     'location.sendfile == true');
like($log, qr/JSTEST PASS tcp_nopush_on/,   'location.tcpNopush == true');
like($log, qr/JSTEST PASS tcp_nodelay_on/,  'location.tcpNodelay == true');
like($log, qr/JSTEST PASS etag_on/,         'location.etag == true');

# timing
like($log, qr/JSTEST PASS keepalive_timeout_60s/,   'location.keepaliveTimeout == 60000');
like($log, qr/JSTEST PASS client_body_timeout_30s/,  'location.clientBodyTimeout == 30000');
like($log, qr/JSTEST PASS send_timeout_20s/,         'location.sendTimeout == 20000');

# counts / sizes
like($log, qr/JSTEST PASS keepalive_requests_500/,  'location.keepaliveRequests == 500');
like($log, qr/JSTEST PASS client_max_body_10m/,     'location.clientMaxBodySize == 10485760');

# string
like($log, qr/JSTEST PASS default_type_html/,  'location.defaultType == text/html');

# errorPage
like($log, qr/JSTEST PASS error_page_is_array/,  'location.errorPage is array');
like($log, qr/JSTEST PASS error_page_3_entries/, 'location.errorPage has 3 entries');
like($log, qr/JSTEST PASS ep0_status_404/,       'errorPage[0].status == 404');
like($log, qr/JSTEST PASS ep0_uri_not_found/,    'errorPage[0].uri == /not_found.html');
like($log, qr/JSTEST PASS ep1_status_500/,       'errorPage[1].status == 500');
like($log, qr/JSTEST PASS ep1_uri_error/,        'errorPage[1].uri == /error.html');
like($log, qr/JSTEST PASS ep2_status_502/,       'errorPage[2].status == 502');
like($log, qr/JSTEST PASS ep2_uri_error/,        'errorPage[2].uri == /error.html');

# alias
like($log, qr/JSTEST PASS root_alias_null/,   'root location.alias == null');
like($log, qr/JSTEST PASS static_alias_set/,  'alias location.alias is set');

# internal
like($log, qr/JSTEST PASS root_not_internal/,     'root location.internal == false');
like($log, qr/JSTEST PASS internal_loc_internal/,  'internal location.internal == true');
