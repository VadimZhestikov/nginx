#!/usr/bin/perl

# Stage 10: NginxProxyCache — 7 property setters
#
#   minUses           number
#   lock              boolean
#   lockTimeout       ms
#   lockAge           ms
#   revalidate        boolean
#   convertHead       boolean
#   backgroundUpdate  boolean
#
# Tests:
#   1.  initial minUses=1 from config
#   2.  initial lock=false
#   3.  set all 7 fields — no error
#   4.  minUses updated
#   5.  lock updated to true
#   6.  lockTimeout updated
#   7.  revalidate updated
#   8.  convertHead updated
#   9.  backgroundUpdate updated
#  10.  persistence: second /read/ shows new values

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy cache/)->plan(10);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    proxy_cache_path %%TESTDIR%%/cache levels=1:2 keys_zone=zone1:1m;

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /proxy/ {
            proxy_pass        http://127.0.0.1:8081/;
            proxy_cache       zone1;
            proxy_cache_min_uses 1;
        }

        location /read/  { }
        location /set/   { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const pc = loc('/proxy/').proxy.cache;

// /read/ — snapshot proxy_cache fields
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        minUses:          pc.minUses,
        lock:             pc.lock,
        lockTimeout:      pc.lockTimeout,
        lockAge:          pc.lockAge,
        revalidate:       pc.revalidate,
        convertHead:      pc.convertHead,
        backgroundUpdate: pc.backgroundUpdate,
    }));
};

// /set/ — write all fields
loc('/set/').handler = r => {
    pc.minUses          = 3;
    pc.lock             = true;
    pc.lockTimeout      = 10000;
    pc.lockAge          = 30000;
    pc.revalidate       = true;
    pc.convertHead      = false;
    pc.backgroundUpdate = true;
    r.respond(200, {}, 'ok');
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"minUses":1/,    'initial minUses is 1');
like($r0, qr/"lock":false/,   'initial lock is false');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all proxy_cache setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"minUses":3/,              'minUses updated to 3');
like($r1, qr/"lock":true/,             'lock updated to true');
like($r1, qr/"lockTimeout":10000/,     'lockTimeout updated');
like($r1, qr/"revalidate":true/,       'revalidate updated to true');
like($r1, qr/"convertHead":false/,     'convertHead updated to false');
like($r1, qr/"backgroundUpdate":true/, 'backgroundUpdate updated to true');

# ---- Persistence ----
like(http_get('/read/'), qr/"minUses":3/, 'changes persist on next request');

$t->stop();
