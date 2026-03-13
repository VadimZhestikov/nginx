#!/usr/bin/perl

# Stage 13: location.alias setter — runtime-writable alias path
#
# Tests:
#   1.  initial alias == "/data/files/" (from alias directive)
#   2.  root location returns null for alias
#   3.  set new alias path — no error
#   4.  after set: alias reflects new path
#   5.  persistence: second /read/ shows new alias
#   6.  setting alias on a root location throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

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

        location /files/ {
            alias /data/files/;
        }

        location /root/ {
            root /data/root;
        }

        location /read/   { }
        location /set/    { }
        location /badrw/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const aliasLoc = loc('/files/');
const rootLoc  = loc('/root/');

// /read/ — snapshot alias
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        alias: aliasLoc.alias,
        root:  rootLoc.alias,
    }));
};

// /set/ — update alias path
loc('/set/').handler = r => {
    aliasLoc.alias = '/srv/www/files/';
    r.respond(200, {}, 'ok');
};

// /badrw/ — alias on a root location throws TypeError
loc('/badrw/').handler = r => {
    try {
        rootLoc.alias = '/should/fail/';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"alias":"\/data\/files\/"/, 'initial alias is /data/files/');
like($r0, qr/"root":null/,              'root location alias is null');

# ---- Apply write ----
like(http_get('/set/'), qr/ok/, 'alias setter executes without error');

my $r1 = http_get('/read/');
like($r1, qr/"alias":"\/srv\/www\/files\/"/, 'after set: alias updated');

# ---- Persistence ----
like(http_get('/read/'), qr/"alias":"\/srv\/www\/files\/"/, 'changes persist');

# ---- Error path ----
like(http_get('/badrw/'), qr/error:/, 'alias on root location throws TypeError');

$t->stop();
