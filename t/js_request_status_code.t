#!/usr/bin/perl

# Stage 41: r.statusCode r/w property + r.respond() optional arguments
#
# r.statusCode  — get/set the response status before headers are sent
# r.respond()   — all three args are now optional:
#                   r.respond()          → uses staged status (or 200) + empty body
#                   r.respond(404)       → status only, empty body
#                   r.respond(200, {})   → status + extra headers, empty body
#                   r.respond(200,{},s)  → existing 3-arg form (unchanged)
#
# Tests:
#   1.  r.statusCode default is 0 before any assignment
#   2.  r.statusCode = 404 persists (getter reads it back)
#   3.  r.respond() with no args uses staged statusCode
#   4.  r.respond() with no args defaults to 200 when statusCode not set
#   5.  r.respond(404) — 1-arg form sends correct status
#   6.  r.respond(200, {}) — 2-arg form with no body sends 200
#   7.  staged setHeader + r.respond() — headers appear in response
#   8.  staged statusCode + r.respond() — body is empty
#   9.  r.statusCode setter throws after headers sent
#  10.  existing 3-arg r.respond() still works

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

        location /default/   { }
        location /staged/    { }
        location /no_args/   { }
        location /one_arg/   { }
        location /two_args/  { }
        location /combined/  { }
        location /three/     { }
        location /after_set/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const s = nginx.http.servers[0];
const loc = p => s.locations.find(l => l.path === p);

// /default/ — read statusCode before any assignment (should be 0)
loc('/default/').handler = r => {
    const before = r.statusCode;
    r.statusCode = 404;
    const after = r.statusCode;
    r.respond(200, {}, JSON.stringify({ before, after }));
};

// /staged/ — staged statusCode + r.respond() with no args
loc('/staged/').handler = r => {
    r.statusCode = 201;
    r.respond();
};

// /no_args/ — no statusCode set, r.respond() defaults to 200
loc('/no_args/').handler = r => {
    r.respond();
};

// /one_arg/ — 1-arg respond
loc('/one_arg/').handler = r => {
    r.respond(404);
};

// /two_args/ — 2-arg respond
loc('/two_args/').handler = r => {
    r.respond(202, {});
};

// /combined/ — setHeader + respond() with no args
loc('/combined/').handler = r => {
    r.statusCode = 200;
    r.setHeader('X-Stage', '41');
    r.respond();
};

// /three/ — existing 3-arg form unchanged
loc('/three/').handler = r => {
    r.respond(200, { 'X-Old': 'yes' }, 'hello');
};

// /after_set/ — statusCode setter after respond must throw
loc('/after_set/').handler = r => {
    r.respond(200, {}, 'done');
    let threw = false;
    try { r.statusCode = 500; } catch(e) { threw = true; }
    nginx.log(6, 'STATUS_AFTER_RESPOND_THREW:' + threw);
};
JS

$t->run();

# 1-2: statusCode getter/setter
my $r1 = http_get('/default/');
like($r1, qr/"before":0/,   'statusCode default is 0');
like($r1, qr/"after":404/,  'statusCode setter persists');

# 3: staged statusCode used by respond()
my $r2 = http_get('/staged/');
like($r2, qr|HTTP/1.1 201|, 'respond() uses staged statusCode 201');

# 4: respond() defaults to 200 when no statusCode set
my $r3 = http_get('/no_args/');
like($r3, qr|HTTP/1.1 200|, 'respond() defaults to 200 when statusCode not staged');

# 5: 1-arg respond
my $r4 = http_get('/one_arg/');
like($r4, qr|HTTP/1.1 404|, 'respond(404) sends 404');

# 6: 2-arg respond
my $r5 = http_get('/two_args/');
like($r5, qr|HTTP/1.1 202|, 'respond(202, {}) sends 202');

# 7: staged header appears when using respond()
my $r6 = http_get('/combined/');
like($r6, qr/X-Stage: 41/i, 'setHeader + respond() — header in response');

# 8: body is empty for no-arg respond
like($r6, qr/\r\n\r\n$/, 'respond() with no body arg sends empty body');

# 9: statusCode setter throws after respond
http_get('/after_set/');
my $log = $t->read_file('error.log');
like($log, qr/STATUS_AFTER_RESPOND_THREW:true/,
     'statusCode setter throws after headers sent');

# 10: 3-arg respond still works
my $r7 = http_get('/three/');
like($r7, qr/X-Old: yes/i, '3-arg respond: header present');
like($r7, qr/hello/,       '3-arg respond: body present');

$t->stop();
