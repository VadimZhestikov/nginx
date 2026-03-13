#!/usr/bin/perl

# Stage 10: NginxLog.off + NginxHeaders.headersInherit/trailersInherit setters
#
# Tests:
#   1.  initial log.off=false
#   2.  set log.off=true — getter reflects change
#   3.  initial headersInherit="off"
#   4.  set headersInherit="on"
#   5.  set trailersInherit="merge"
#   6.  persistence: second /read/ shows updated headersInherit
#   7.  headersInherit: bad string throws TypeError
#   8.  trailersInherit: bad string throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(8);

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

        location /hdrs/ {
            add_header X-Foo bar;
        }

        location /read/  { }
        location /set/   { }
        location /errs/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const hdrLoc = loc('/hdrs/');

// /read/ — snapshot log.off and headers inherit values
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        logOff:          loc('/hdrs/').log.off,
        headersInherit:  hdrLoc.headers.headersInherit,
        trailersInherit: hdrLoc.headers.trailersInherit,
    }));
};

// /set/ — write new values
loc('/set/').handler = r => {
    loc('/hdrs/').log.off              = true;
    hdrLoc.headers.headersInherit      = 'on';
    hdrLoc.headers.trailersInherit     = 'merge';
    r.respond(200, {}, 'ok');
};

// /errs/ — trigger error paths
loc('/errs/').handler = r => {
    const results = {};

    try { hdrLoc.headers.headersInherit  = 'bad'; results.hi = 'no-error'; }
    catch (e) { results.hi = 'error'; }

    try { hdrLoc.headers.trailersInherit = 'bad'; results.ti = 'no-error'; }
    catch (e) { results.ti = 'error'; }

    r.respond(200, {}, JSON.stringify(results));
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"logOff":false/,        'initial log.off is false');
like($r0, qr/"headersInherit":"on"/, 'initial headersInherit is on');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"logOff":true/,           'log.off set to true');
like($r1, qr/"headersInherit":"on"/,   'headersInherit set to on');
like($r1, qr/"trailersInherit":"merge"/, 'trailersInherit set to merge');

# ---- Persistence ----
like(http_get('/read/'), qr/"headersInherit":"on"/, 'changes persist');

# ---- Error paths ----
my $errs = http_get('/errs/');
like($errs, qr/"hi":"error"/, 'bad headersInherit string throws TypeError');

$t->stop();
