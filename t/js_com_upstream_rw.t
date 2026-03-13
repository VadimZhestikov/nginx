#!/usr/bin/perl

# Stage 0-C: timeout and flag fields on NginxProxy, NginxFastcgi, NginxScgi,
# NginxUwsgi, and NginxMemcached are now writable.
#
# For each backend object the test:
#   1. Sets fields at config phase (init.js)
#   2. Reads them back via a request handler
#   3. Mutates them from inside a request handler
#   4. Reads again to verify the runtime change stuck

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(40);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream dummy_proxy     { server 127.0.0.1:%%PORT_8091%%; }
    upstream dummy_fastcgi   { server 127.0.0.1:%%PORT_8092%%; }
    upstream dummy_scgi      { server 127.0.0.1:%%PORT_8093%%; }
    upstream dummy_uwsgi     { server 127.0.0.1:%%PORT_8094%%; }
    upstream dummy_memcached { server 127.0.0.1:%%PORT_8095%%; }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /proxy/ {
            proxy_pass http://dummy_proxy;
        }
        location /fastcgi/ {
            fastcgi_pass dummy_fastcgi;
        }
        location /scgi/ {
            scgi_pass dummy_scgi;
        }
        location /uwsgi/ {
            uwsgi_pass dummy_uwsgi;
        }
        location /memcached/ {
            set $memcached_key "k";
            memcached_pass dummy_memcached;
        }

        location /read/   { }
        location /mutate/ { }
        location /reread/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// ---- Helpers to gather all writable fields from each backend ----

function proxyFields(l) {
    const p = l.proxy;
    return {
        connectTimeout:      p.connectTimeout,
        sendTimeout:         p.sendTimeout,
        readTimeout:         p.readTimeout,
        buffering:           p.buffering,
        requestBuffering:    p.requestBuffering,
        interceptErrors:     p.interceptErrors,
        nextUpstreamTries:   p.nextUpstreamTries,
        nextUpstreamTimeout: p.nextUpstreamTimeout,
    };
}

function fastcgiFields(l) {
    const f = l.fastcgi;
    return {
        connectTimeout:   f.connectTimeout,
        sendTimeout:      f.sendTimeout,
        readTimeout:      f.readTimeout,
        buffering:        f.buffering,
        requestBuffering: f.requestBuffering,
        interceptErrors:  f.interceptErrors,
    };
}

function scgiFields(l) {
    const s = l.scgi;
    return {
        connectTimeout: s.connectTimeout,
        sendTimeout:    s.sendTimeout,
        readTimeout:    s.readTimeout,
    };
}

function uwsgiFields(l) {
    const u = l.uwsgi;
    return {
        connectTimeout: u.connectTimeout,
        sendTimeout:    u.sendTimeout,
        readTimeout:    u.readTimeout,
        modifier1:      u.modifier1,
        modifier2:      u.modifier2,
    };
}

function memcachedFields(l) {
    const m = l.memcached;
    return {
        connectTimeout: m.connectTimeout,
        sendTimeout:    m.sendTimeout,
        readTimeout:    m.readTimeout,
    };
}

// ---- Set known values at config phase ----

const pl  = loc('/proxy/');
const fl  = loc('/fastcgi/');
const sl  = loc('/scgi/');
const ul  = loc('/uwsgi/');
const ml  = loc('/memcached/');

pl.proxy.connectTimeout      = 1111;
pl.proxy.sendTimeout         = 2222;
pl.proxy.readTimeout         = 3333;
pl.proxy.buffering           = false;
pl.proxy.requestBuffering    = false;
pl.proxy.interceptErrors     = true;
pl.proxy.nextUpstreamTries   = 5;
pl.proxy.nextUpstreamTimeout = 4444;

fl.fastcgi.connectTimeout    = 1112;
fl.fastcgi.sendTimeout       = 2223;
fl.fastcgi.readTimeout       = 3334;
fl.fastcgi.buffering         = false;
fl.fastcgi.requestBuffering  = false;
fl.fastcgi.interceptErrors   = true;

sl.scgi.connectTimeout       = 1113;
sl.scgi.sendTimeout          = 2224;
sl.scgi.readTimeout          = 3335;

ul.uwsgi.connectTimeout      = 1114;
ul.uwsgi.sendTimeout         = 2225;
ul.uwsgi.readTimeout         = 3336;
ul.uwsgi.modifier1           = 7;
ul.uwsgi.modifier2           = 9;

ml.memcached.connectTimeout  = 1115;
ml.memcached.sendTimeout     = 2226;
ml.memcached.readTimeout     = 3337;

// ---- /read/ — echo back configured values ----
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        proxy:     proxyFields(pl),
        fastcgi:   fastcgiFields(fl),
        scgi:      scgiFields(sl),
        uwsgi:     uwsgiFields(ul),
        memcached: memcachedFields(ml),
    }));
};

// ---- /mutate/ — change every field from a request handler ----
loc('/mutate/').handler = r => {
    pl.proxy.connectTimeout      = 9001;
    pl.proxy.sendTimeout         = 9002;
    pl.proxy.readTimeout         = 9003;
    pl.proxy.buffering           = true;
    pl.proxy.requestBuffering    = true;
    pl.proxy.interceptErrors     = false;
    pl.proxy.nextUpstreamTries   = 10;
    pl.proxy.nextUpstreamTimeout = 9004;

    fl.fastcgi.connectTimeout    = 9011;
    fl.fastcgi.sendTimeout       = 9012;
    fl.fastcgi.readTimeout       = 9013;
    fl.fastcgi.buffering         = true;
    fl.fastcgi.requestBuffering  = true;
    fl.fastcgi.interceptErrors   = false;

    sl.scgi.connectTimeout       = 9021;
    sl.scgi.sendTimeout          = 9022;
    sl.scgi.readTimeout          = 9023;

    ul.uwsgi.connectTimeout      = 9031;
    ul.uwsgi.sendTimeout         = 9032;
    ul.uwsgi.readTimeout         = 9033;
    ul.uwsgi.modifier1           = 42;
    ul.uwsgi.modifier2           = 43;

    ml.memcached.connectTimeout  = 9041;
    ml.memcached.sendTimeout     = 9042;
    ml.memcached.readTimeout     = 9043;

    r.respond(200, {}, 'mutated');
};

// ---- /reread/ — echo back after mutation ----
loc('/reread/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        proxy:     proxyFields(pl),
        fastcgi:   fastcgiFields(fl),
        scgi:      scgiFields(sl),
        uwsgi:     uwsgiFields(ul),
        memcached: memcachedFields(ml),
    }));
};
JS

$t->run();

# ---- Phase 1: config-phase writes are visible at request time ----

my $r1 = http_get('/read/');

# proxy
like($r1, qr/"connectTimeout":1111/,       'proxy.connectTimeout=1111 at config phase');
like($r1, qr/"sendTimeout":2222/,           'proxy.sendTimeout=2222 at config phase');
like($r1, qr/"readTimeout":3333/,           'proxy.readTimeout=3333 at config phase');
like($r1, qr/"buffering":false/,            'proxy.buffering=false at config phase');
like($r1, qr/"interceptErrors":true/,       'proxy.interceptErrors=true at config phase');
like($r1, qr/"nextUpstreamTries":5/,        'proxy.nextUpstreamTries=5 at config phase');
like($r1, qr/"nextUpstreamTimeout":4444/,   'proxy.nextUpstreamTimeout=4444 at config phase');

# fastcgi
like($r1, qr/"connectTimeout":1112/,        'fastcgi.connectTimeout=1112 at config phase');
like($r1, qr/"interceptErrors":true/,       'fastcgi.interceptErrors=true at config phase');

# scgi
like($r1, qr/"connectTimeout":1113/,        'scgi.connectTimeout=1113 at config phase');
like($r1, qr/"sendTimeout":2224/,           'scgi.sendTimeout=2224 at config phase');
like($r1, qr/"readTimeout":3335/,           'scgi.readTimeout=3335 at config phase');

# uwsgi
like($r1, qr/"connectTimeout":1114/,        'uwsgi.connectTimeout=1114 at config phase');
like($r1, qr/"modifier1":7/,               'uwsgi.modifier1=7 at config phase');
like($r1, qr/"modifier2":9/,               'uwsgi.modifier2=9 at config phase');

# memcached
like($r1, qr/"connectTimeout":1115/,        'memcached.connectTimeout=1115 at config phase');
like($r1, qr/"sendTimeout":2226/,           'memcached.sendTimeout=2226 at config phase');
like($r1, qr/"readTimeout":3337/,           'memcached.readTimeout=3337 at config phase');

# ---- Phase 2: runtime mutation ----

like(http_get('/mutate/'), qr/mutated/, 'mutation request succeeded');

my $r2 = http_get('/reread/');

# proxy
like($r2, qr/"connectTimeout":9001/,        'proxy.connectTimeout=9001 after mutation');
like($r2, qr/"sendTimeout":9002/,            'proxy.sendTimeout=9002 after mutation');
like($r2, qr/"readTimeout":9003/,            'proxy.readTimeout=9003 after mutation');
like($r2, qr/"buffering":true/,              'proxy.buffering=true after mutation');
like($r2, qr/"interceptErrors":false/,       'proxy.interceptErrors=false after mutation');
like($r2, qr/"nextUpstreamTries":10/,        'proxy.nextUpstreamTries=10 after mutation');
like($r2, qr/"nextUpstreamTimeout":9004/,    'proxy.nextUpstreamTimeout=9004 after mutation');

# fastcgi
like($r2, qr/"connectTimeout":9011/,         'fastcgi.connectTimeout=9011 after mutation');
like($r2, qr/"buffering":true/,              'fastcgi.buffering=true after mutation');
like($r2, qr/"interceptErrors":false/,       'fastcgi.interceptErrors=false after mutation');

# scgi
like($r2, qr/"connectTimeout":9021/,         'scgi.connectTimeout=9021 after mutation');
like($r2, qr/"sendTimeout":9022/,            'scgi.sendTimeout=9022 after mutation');
like($r2, qr/"readTimeout":9023/,            'scgi.readTimeout=9023 after mutation');

# uwsgi
like($r2, qr/"connectTimeout":9031/,         'uwsgi.connectTimeout=9031 after mutation');
like($r2, qr/"modifier1":42/,               'uwsgi.modifier1=42 after mutation');
like($r2, qr/"modifier2":43/,               'uwsgi.modifier2=43 after mutation');

# memcached
like($r2, qr/"connectTimeout":9041/,         'memcached.connectTimeout=9041 after mutation');
like($r2, qr/"sendTimeout":9042/,            'memcached.sendTimeout=9042 after mutation');
like($r2, qr/"readTimeout":9043/,            'memcached.readTimeout=9043 after mutation');

# Sanity: worker alive
like(http_get('/read/'), qr/200/, 'worker alive after all mutations');
like(http_get('/read/'), qr/200/, 'worker still alive on second check');

$t->stop();
