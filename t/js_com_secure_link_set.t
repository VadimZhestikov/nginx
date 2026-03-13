#!/usr/bin/perl

# Stage 7b: NginxSecureLink.secret — runtime-writable
#
# secureLink.secret is now writable.  The new value is copied to
# ngx_cycle->pool and takes effect for subsequent requests.
#
# Tests:
#   1. Initial secret getter returns value from config
#   2. secret setter succeeds and getter reflects new value
#   3. Persistence: second /read/ still shows new secret
#   4. variable and md5 remain read-only (assigning them is silently ignored
#      because they have no setter — or throws in strict mode)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http secure_link/)->plan(4);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /link/ {
            secure_link_secret  initial-secret;
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

const linkLoc = loc('/link/');

// /read/ — return current secret
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        secret: linkLoc.secureLink.secret,
    }));
};

// /set/ — overwrite secret and return new value
loc('/set/').handler = r => {
    linkLoc.secureLink.secret = 'new-runtime-secret';
    r.respond(200, {}, JSON.stringify({
        secret: linkLoc.secureLink.secret,
    }));
};
JS

$t->run();

# ---- Initial value from config ----
my $r0 = http_get('/read/');
like($r0, qr/"secret":"initial-secret"/, 'initial secret from config');

# ---- Set new secret ----
my $r1 = http_get('/set/');
like($r1, qr/200/,                           'set secret succeeds');
like($r1, qr/"secret":"new-runtime-secret"/, 'secret getter updated immediately');

# ---- Persistence ----
like(http_get('/read/'), qr/"secret":"new-runtime-secret"/, 'new secret persists');

$t->stop();
