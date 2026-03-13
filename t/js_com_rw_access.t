#!/usr/bin/perl

# Stage 11: access.rules setter — runtime-writable IPv4 allow/deny rules
#
# Tests:
#   1.  initial rules.length is 2 (allow 127.0.0.1; deny all)
#   2.  initial rules[0].deny == false
#   3.  initial rules[1].cidr == "all"
#   4.  set new rules — no error
#   5.  after set: rules.length == 2
#   6.  after set: rules[0].cidr == "10.0.0.0/8"
#   7.  after set: rules[1].deny == true, cidr == "all"
#   8.  persistence: second /read/ still shows new rules
#   9.  set [] clears rules (rules.length == 0)
#  10.  invalid CIDR throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http access/)->plan(10);

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

        location /acc/ {
            allow  127.0.0.1;
            deny   all;
        }

        location /read/   { }
        location /set/    { }
        location /clear/  { }
        location /badcid/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const accLoc = loc('/acc/');

// /read/ — snapshot rules
loc('/read/').handler = r => {
    const rules = accLoc.access.rules;
    r.respond(200, {}, JSON.stringify({
        len:   rules.length,
        r0:    rules[0] || null,
        r1:    rules[1] || null,
    }));
};

// /set/ — replace rules
loc('/set/').handler = r => {
    accLoc.access.rules = [
        { deny: false, cidr: '10.0.0.0/8' },
        { deny: true,  cidr: 'all' },
    ];
    r.respond(200, {}, 'ok');
};

// /clear/ — set empty array
loc('/clear/').handler = r => {
    accLoc.access.rules = [];
    r.respond(200, {}, JSON.stringify({
        len: accLoc.access.rules.length,
    }));
};

// /badcid/ — invalid CIDR throws TypeError
loc('/badcid/').handler = r => {
    try {
        accLoc.access.rules = [{ deny: true, cidr: 'notanip' }];
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"len":2/,            'initial rules.length is 2');
like($r0, qr/"deny":false/,       'initial rules[0].deny is false');
like($r0, qr/"cidr":"all"/,       'initial rules[1].cidr is "all"');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/,   'rules setter executes without error');

my $r1 = http_get('/read/');
like($r1, qr/"len":2/,                      'after set: rules.length is 2');
like($r1, qr/"cidr":"10\.0\.0\.0\/8"/,      'after set: rules[0].cidr is 10.0.0.0/8');
like($r1, qr/\{"deny":true,"cidr":"all"\}/, 'after set: rules[1] is deny all');

# ---- Persistence ----
like(http_get('/read/'), qr/"cidr":"10\.0\.0\.0\/8"/, 'changes persist');

# ---- Clear ----
like(http_get('/clear/'), qr/"len":0/, 'set [] clears rules');

# ---- Error path ----
like(http_get('/badcid/'), qr/error:/, 'invalid CIDR throws TypeError');

$t->stop();
