#!/usr/bin/perl

# Stage 0-B: NginxLocation core scalar fields are now writable.
# Tests cover:
#   sendfile, tcpNopush, tcpNodelay, etag          (ngx_flag_t bool)
#   keepaliveTimeout, keepaliveRequests, keepaliveTime   (numeric)
#   clientMaxBodySize, clientBodyTimeout, sendTimeout    (numeric)
#   defaultType                                    (ngx_str_t string)
#
# Each field is:
#   1. set at config phase (init.js)
#   2. read back via a request handler — verifies the value stuck
#   3. mutated again inside a request handler — verifies runtime writes work

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(24);

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

        location /read/   { }
        location /mutate/ { }
        location /reread/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const loc = nginx.http.servers[0].locations.find(l => l.path === '/read/');
const mut = nginx.http.servers[0].locations.find(l => l.path === '/mutate/');
const rer = nginx.http.servers[0].locations.find(l => l.path === '/reread/');

// ---- Set all target fields to known values at config phase ----

loc.sendfile          = true;
loc.tcpNopush         = true;
loc.tcpNodelay        = false;
loc.etag              = false;
loc.keepaliveTimeout  = 12000;
loc.keepaliveRequests = 200;
loc.keepaliveTime     = 3600000;
loc.clientMaxBodySize = 2 * 1024 * 1024;
loc.clientBodyTimeout = 15000;
loc.sendTimeout       = 20000;
loc.defaultType       = 'text/plain';

// ---- /read/ — echo back the configured values ----
loc.handler = r => {
    const l = nginx.http.servers[0].locations.find(x => x.path === '/read/');
    r.respond(200, {}, JSON.stringify({
        sendfile:          l.sendfile,
        tcpNopush:         l.tcpNopush,
        tcpNodelay:        l.tcpNodelay,
        etag:              l.etag,
        keepaliveTimeout:  l.keepaliveTimeout,
        keepaliveRequests: l.keepaliveRequests,
        keepaliveTime:     l.keepaliveTime,
        clientMaxBodySize: l.clientMaxBodySize,
        clientBodyTimeout: l.clientBodyTimeout,
        sendTimeout:       l.sendTimeout,
        defaultType:       l.defaultType,
    }));
};

// ---- /mutate/ — change every field from inside a request handler ----
mut.handler = r => {
    const l = nginx.http.servers[0].locations.find(x => x.path === '/read/');
    l.sendfile          = false;
    l.tcpNopush         = false;
    l.tcpNodelay        = true;
    l.etag              = true;
    l.keepaliveTimeout  = 99000;
    l.keepaliveRequests = 777;
    l.keepaliveTime     = 7200000;
    l.clientMaxBodySize = 4 * 1024 * 1024;
    l.clientBodyTimeout = 55000;
    l.sendTimeout       = 66000;
    l.defaultType       = 'application/json';
    r.respond(200, {}, 'mutated');
};

// ---- /reread/ — echo back values after /mutate/ has run ----
rer.handler = r => {
    const l = nginx.http.servers[0].locations.find(x => x.path === '/read/');
    r.respond(200, {}, JSON.stringify({
        sendfile:          l.sendfile,
        tcpNopush:         l.tcpNopush,
        tcpNodelay:        l.tcpNodelay,
        etag:              l.etag,
        keepaliveTimeout:  l.keepaliveTimeout,
        keepaliveRequests: l.keepaliveRequests,
        keepaliveTime:     l.keepaliveTime,
        clientMaxBodySize: l.clientMaxBodySize,
        clientBodyTimeout: l.clientBodyTimeout,
        sendTimeout:       l.sendTimeout,
        defaultType:       l.defaultType,
    }));
};
JS

$t->run();

# ---- Phase 1: values set at config phase are visible at request time ----

my $r1 = http_get('/read/');

like($r1, qr/"sendfile":true/,          'sendfile=true set at config phase');
like($r1, qr/"tcpNopush":true/,         'tcpNopush=true set at config phase');
like($r1, qr/"tcpNodelay":false/,        'tcpNodelay=false set at config phase');
like($r1, qr/"etag":false/,              'etag=false set at config phase');
like($r1, qr/"keepaliveTimeout":12000/,  'keepaliveTimeout=12000 set at config phase');
like($r1, qr/"keepaliveRequests":200/,   'keepaliveRequests=200 set at config phase');
like($r1, qr/"keepaliveTime":3600000/,   'keepaliveTime=3600000 set at config phase');
like($r1, qr/"clientMaxBodySize":2097152/,'clientMaxBodySize=2MB set at config phase');
like($r1, qr/"clientBodyTimeout":15000/, 'clientBodyTimeout=15000 set at config phase');
like($r1, qr/"sendTimeout":20000/,       'sendTimeout=20000 set at config phase');
like($r1, qr/"defaultType":"text\/plain"/, 'defaultType="text/plain" set at config phase');

# ---- Phase 2: mutate from a request handler ----

like(http_get('/mutate/'), qr/mutated/, 'mutation request succeeded');

my $r2 = http_get('/reread/');

like($r2, qr/"sendfile":false/,           'sendfile=false after runtime mutation');
like($r2, qr/"tcpNopush":false/,          'tcpNopush=false after runtime mutation');
like($r2, qr/"tcpNodelay":true/,           'tcpNodelay=true after runtime mutation');
like($r2, qr/"etag":true/,                 'etag=true after runtime mutation');
like($r2, qr/"keepaliveTimeout":99000/,    'keepaliveTimeout=99000 after runtime mutation');
like($r2, qr/"keepaliveRequests":777/,     'keepaliveRequests=777 after runtime mutation');
like($r2, qr/"keepaliveTime":7200000/,     'keepaliveTime=7200000 after runtime mutation');
like($r2, qr/"clientMaxBodySize":4194304/, 'clientMaxBodySize=4MB after runtime mutation');
like($r2, qr/"clientBodyTimeout":55000/,   'clientBodyTimeout=55000 after runtime mutation');
like($r2, qr/"sendTimeout":66000/,         'sendTimeout=66000 after runtime mutation');
like($r2, qr/"defaultType":"application\/json"/, 'defaultType="application/json" after mutation');

# Sanity: worker still alive
like(http_get('/read/'), qr/200/, 'worker alive after all mutations');

$t->stop();
