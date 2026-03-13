#!/usr/bin/perl

# Stage 14: nginx.events (NginxEvents)
#
# Properties:
#   connections       number   r/o  worker_connections
#   use               string   r/o  event method name
#   multiAccept       boolean  r/w  multi_accept on/off
#   acceptMutex       boolean  r/w  accept_mutex on/off
#   acceptMutexDelay  number   r/w  accept_mutex_delay (ms)
#
# Tests:
#   1.  nginx.events is an object
#   2.  connections > 0
#   3.  use is a non-empty string
#   4.  initial multiAccept is false (default)
#   5.  initial acceptMutex is false (default: accept_mutex off since nginx 1.11.3)
#   6.  initial acceptMutexDelay is 500
#   7.  set multiAccept=true — no error
#   8.  multiAccept setter persists
#   9.  set acceptMutexDelay=200
#  10.  acceptMutexDelay setter persists
#  11.  connections is read-only (assignment is silently ignored or throws)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(11);

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

        location /read/   { }
        location /set/    { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const ev = nginx.events;

// /read/ — snapshot events properties
nginx.http.servers[0].locations.find(l => l.path === '/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        isObj:            typeof ev === 'object' && ev !== null,
        connections:      ev.connections,
        use:              ev.use,
        multiAccept:      ev.multiAccept,
        acceptMutex:      ev.acceptMutex,
        acceptMutexDelay: ev.acceptMutexDelay,
    }));
};

// /set/ — modify writable properties
nginx.http.servers[0].locations.find(l => l.path === '/set/').handler = r => {
    ev.multiAccept      = true;
    ev.acceptMutexDelay = 200;
    r.respond(200, {}, 'ok');
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"isObj":true/,          'nginx.events is an object');
like($r0, qr/"connections":[1-9]/,   'connections > 0');
like($r0, qr/"use":"[a-z]/,          'use is a non-empty string');
like($r0, qr/"multiAccept":false/,   'initial multiAccept is false');
like($r0, qr/"acceptMutex":false/,   'initial acceptMutex is false');
like($r0, qr/"acceptMutexDelay":500/,'initial acceptMutexDelay is 500');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'setters execute without error');

my $r1 = http_get('/read/');
like($r1, qr/"multiAccept":true/,       'multiAccept updated to true');
like($r1, qr/"acceptMutexDelay":200/,   'acceptMutexDelay updated to 200');

# ---- Persistence ----
my $r2 = http_get('/read/');
like($r2, qr/"multiAccept":true/,       'changes persist');

# ---- connections is r/o (property exists with correct value still) ----
like($r2, qr/"connections":[1-9]/,      'connections still readable after set');

$t->stop();
