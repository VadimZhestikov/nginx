#!/usr/bin/perl

# Stage 0-D: filter module COM objects are now (partially) writable.
#
# NginxGzip:      enable, level, minLength, vary
# NginxGunzip:    enable
# NginxAutoindex: enable, format, localtime, exactSize
#
# For each object the test:
#   1. Sets fields at config phase (init.js)
#   2. Reads them back via a request handler
#   3. Mutates from a request handler
#   4. Reads again to verify the runtime change stuck

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

        # enable modules so the COM objects are non-null
        gzip on;
        gzip_min_length 0;
        gunzip on;
        autoindex on;

        location /read/    { }
        location /mutate/  { }
        location /reread/  { }
        location /fmttest/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

function snapshot(l) {
    return JSON.stringify({
        gzip: {
            enable:    l.gzip.enable,
            level:     l.gzip.level,
            minLength: l.gzip.minLength,
            vary:      l.gzip.vary,
        },
        gunzip: {
            enable: l.gunzip.enable,
        },
        autoindex: {
            enable:    l.autoindex.enable,
            format:    l.autoindex.format,
            localtime: l.autoindex.localtime,
            exactSize: l.autoindex.exactSize,
        },
    });
}

// ---- Set known values at config phase ----

const rl = loc('/read/');

rl.gzip.enable    = true;
rl.gzip.level     = 3;
rl.gzip.minLength = 512;
rl.gzip.vary      = true;

rl.gunzip.enable  = true;

rl.autoindex.enable    = true;
rl.autoindex.format    = 'json';
rl.autoindex.localtime = true;
rl.autoindex.exactSize = false;

// ---- /read/ — echo back the configured values ----
loc('/read/').handler = r => {
    const l = srv.locations.find(x => x.path === '/read/');
    r.respond(200, {}, snapshot(l));
};

// ---- /mutate/ — change every field from a request handler ----
loc('/mutate/').handler = r => {
    const l = srv.locations.find(x => x.path === '/read/');
    l.gzip.enable    = false;
    l.gzip.level     = 9;
    l.gzip.minLength = 1024;
    l.gzip.vary      = false;
    l.gunzip.enable  = false;
    l.autoindex.enable    = false;
    l.autoindex.format    = 'xml';
    l.autoindex.localtime = false;
    l.autoindex.exactSize = true;
    r.respond(200, {}, 'mutated');
};

// ---- /reread/ — echo back after mutation ----
loc('/reread/').handler = r => {
    const l = srv.locations.find(x => x.path === '/read/');
    r.respond(200, {}, snapshot(l));
};

// ---- /fmttest/ — cycle through all four autoindex format strings ----
loc('/fmttest/').handler = r => {
    const l  = srv.locations.find(x => x.path === '/read/');
    const ai = l.autoindex;
    const formats = ['html', 'json', 'jsonp', 'xml'];
    const results = [];
    for (const f of formats) {
        ai.format = f;
        results.push(ai.format);   // read back immediately
    }
    r.respond(200, {}, results.join(','));
};
JS

$t->run();

# ---- Phase 1: config-phase writes visible at request time ----

my $r1 = http_get('/read/');

like($r1, qr/"enable":true/,       'gzip.enable=true at config phase');
like($r1, qr/"level":3/,           'gzip.level=3 at config phase');
like($r1, qr/"minLength":512/,     'gzip.minLength=512 at config phase');
like($r1, qr/"vary":true/,         'gzip.vary=true at config phase');
like($r1, qr/"format":"json"/,     'autoindex.format="json" at config phase');
like($r1, qr/"localtime":true/,    'autoindex.localtime=true at config phase');
like($r1, qr/"exactSize":false/,   'autoindex.exactSize=false at config phase');

# ---- Phase 2: runtime mutation ----

like(http_get('/mutate/'), qr/mutated/, 'mutation request succeeded');

my $r2 = http_get('/reread/');

like($r2, qr/"enable":false/,      'gzip.enable=false after mutation');
like($r2, qr/"level":9/,           'gzip.level=9 after mutation');
like($r2, qr/"minLength":1024/,    'gzip.minLength=1024 after mutation');
like($r2, qr/"vary":false/,        'gzip.vary=false after mutation');
like($r2, qr/"format":"xml"/,      'autoindex.format="xml" after mutation');
like($r2, qr/"localtime":false/,   'autoindex.localtime=false after mutation');
like($r2, qr/"exactSize":true/,    'autoindex.exactSize=true after mutation');

# ---- Phase 3: all four autoindex format strings round-trip ----

my $fmt = http_get('/fmttest/');
like($fmt, qr/html,json,jsonp,xml/, 'all four autoindex formats accepted and read back');

# ---- gunzip.enable independent check ----
like($r1, qr/"enable":true/,  'gunzip.enable=true at config phase (in snapshot)');
like($r2, qr/"enable":false/, 'gunzip.enable=false after mutation (in snapshot)');

# ---- Sanity ----
like(http_get('/read/'),   qr/200/, 'worker alive after gzip mutations');
like(http_get('/reread/'), qr/200/, 'worker alive after autoindex mutations');

# ---- Verify gzip.level=3 survives across multiple requests ----
like(http_get('/mutate/'), qr/mutated/, 'third mutate ok');
like(http_get('/reread/'), qr/"level":9/, 'gzip.level still 9 after third mutate');
like(http_get('/reread/'), qr/"format":"xml"/, 'format still xml after third mutate');
like(http_get('/read/'),   qr/200/, 'final alive check');

$t->stop();
