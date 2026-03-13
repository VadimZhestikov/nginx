#!/usr/bin/perl

# Stage 0-E: NginxAddition and NginxMirror fields are now writable.
#
# NginxAddition: addBeforeBody, addAfterBody  (ngx_str_t, dup to pool)
# NginxMirror:   requestBody                  (ngx_flag_t bool)
#
# For each field the test:
#   1. Sets the value at config phase (init.js)
#   2. Reads it back via a request handler
#   3. Mutates it from inside a request handler
#   4. Reads again to verify the runtime change stuck

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http addition/)->plan(12);

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

        # addition directives so the COM object is non-null
        location /read/ {
            add_before_body /hdr;
            add_after_body  /ftr;
        }

        # mirror_request_body so the COM object is non-null
        location /mirror/ {
            mirror              /backend;
            mirror_request_body off;
        }

        location /mutate/  { }
        location /reread/  { }
        location /mread/   { }
        location /mmutate/ { }
        location /mreread/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// ---- Set known values at config phase ----

const al = loc('/read/');
al.addition.addBeforeBody = '/new-header';
al.addition.addAfterBody  = '/new-footer';

const ml = loc('/mirror/');
ml.mirror.requestBody = true;

// ---- /read/ — echo back addition fields ----
loc('/read/').handler = r => {
    const l = srv.locations.find(x => x.path === '/read/');
    r.respond(200, {}, JSON.stringify({
        addBeforeBody: l.addition.addBeforeBody,
        addAfterBody:  l.addition.addAfterBody,
    }));
};

// ---- /mutate/ — change addition fields from a request handler ----
loc('/mutate/').handler = r => {
    const l = srv.locations.find(x => x.path === '/read/');
    l.addition.addBeforeBody = '/runtime-header';
    l.addition.addAfterBody  = '/runtime-footer';
    r.respond(200, {}, 'mutated');
};

// ---- /reread/ — echo back after mutation ----
loc('/reread/').handler = r => {
    const l = srv.locations.find(x => x.path === '/read/');
    r.respond(200, {}, JSON.stringify({
        addBeforeBody: l.addition.addBeforeBody,
        addAfterBody:  l.addition.addAfterBody,
    }));
};

// ---- /mread/ — echo back mirror.requestBody ----
loc('/mread/').handler = r => {
    const l = srv.locations.find(x => x.path === '/mirror/');
    r.respond(200, {}, String(l.mirror.requestBody));
};

// ---- /mmutate/ — change mirror.requestBody from a request handler ----
loc('/mmutate/').handler = r => {
    const l = srv.locations.find(x => x.path === '/mirror/');
    l.mirror.requestBody = false;
    r.respond(200, {}, 'mirror-mutated');
};

// ---- /mreread/ — echo back mirror.requestBody after mutation ----
loc('/mreread/').handler = r => {
    const l = srv.locations.find(x => x.path === '/mirror/');
    r.respond(200, {}, String(l.mirror.requestBody));
};
JS

$t->run();

# ---- NginxAddition ----

my $r1 = http_get('/read/');
like($r1, qr|"addBeforeBody":"/new-header"|, 'addition.addBeforeBody set at config phase');
like($r1, qr|"addAfterBody":"/new-footer"|,  'addition.addAfterBody set at config phase');

like(http_get('/mutate/'), qr/mutated/, 'addition mutation request succeeded');

my $r2 = http_get('/reread/');
like($r2, qr|"addBeforeBody":"/runtime-header"|, 'addBeforeBody updated at runtime');
like($r2, qr|"addAfterBody":"/runtime-footer"|,  'addAfterBody updated at runtime');

# Verify persistence across subsequent requests
like(http_get('/reread/'), qr|/runtime-header|, 'addBeforeBody persists across requests');
like(http_get('/reread/'), qr|/runtime-footer|, 'addAfterBody persists across requests');

# ---- NginxMirror ----

# config phase set requestBody = true
like(http_get('/mread/'),   qr/^HTTP.*true/s,         'mirror.requestBody=true at config phase');

like(http_get('/mmutate/'), qr/mirror-mutated/,        'mirror mutation request succeeded');

like(http_get('/mreread/'), qr/^HTTP.*false/s,         'mirror.requestBody=false after mutation');

# Verify persistence
like(http_get('/mreread/'), qr/false/,                 'requestBody persists as false');

# Sanity: worker alive
like(http_get('/read/'), qr/200/, 'worker alive after all mutations');

$t->stop();
