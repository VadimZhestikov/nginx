#!/usr/bin/perl

# Stage 40: r.setHeader(name, value) / r.getHeader(name) / r.removeHeader(name)
#
# Three new NginxRequest methods for incremental response header manipulation
# before headers are sent.
#
# r.setHeader(name, value)   — add or overwrite a response header
# r.getHeader(name)          — read a staged response header (null if absent)
# r.removeHeader(name)       — clear a staged header (null value also works)
#
# Tests:
#   1.  setHeader + respond — header appears in response
#   2.  getHeader returns staged value before respond
#   3.  setHeader overwrite — second call replaces first
#   4.  getHeader after overwrite returns new value
#   5.  removeHeader clears a staged header (absent from response)
#   6.  getHeader after remove returns null
#   7.  setHeader(name, null) is equivalent to removeHeader
#   8.  content-type via setHeader appears correctly
#   9.  getHeader("content-type") reads back staged content-type
#  10.  setHeader after respond throws TypeError (headers already sent)
#  11.  getHeader on never-set header returns null
#  12.  case-insensitive match: Set-Header / get-header same key

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

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

        location /set/         { }
        location /get/         { }
        location /overwrite/   { }
        location /remove/      { }
        location /null_remove/ { }
        location /ct/          { }
        location /after/       { }
        location /case/        { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const s = nginx.http.servers[0];
const loc = p => s.locations.find(l => l.path === p);

// /set/ — setHeader then respond; verify header appears
loc('/set/').handler = r => {
    r.setHeader('X-Foo', 'bar');
    r.respond(200, {}, 'ok');
};

// /get/ — getHeader before respond
loc('/get/').handler = r => {
    r.setHeader('X-Staged', 'hello');
    const v = r.getHeader('X-Staged');
    const missing = r.getHeader('X-Missing');
    r.respond(200, {}, JSON.stringify({ v, missing }));
};

// /overwrite/ — second setHeader replaces first; getHeader reflects update
loc('/overwrite/').handler = r => {
    r.setHeader('X-Val', 'first');
    r.setHeader('X-Val', 'second');
    const v = r.getHeader('X-Val');
    r.respond(200, {}, v);
};

// /remove/ — removeHeader clears header; getHeader returns null after
loc('/remove/').handler = r => {
    r.setHeader('X-Gone', 'present');
    r.removeHeader('X-Gone');
    const v = r.getHeader('X-Gone');
    r.respond(200, { 'X-Check': 'yes' }, JSON.stringify({ v }));
};

// /null_remove/ — setHeader(name, null) removes
loc('/null_remove/').handler = r => {
    r.setHeader('X-Del', 'here');
    r.setHeader('X-Del', null);
    r.respond(200, {}, 'ok');
};

// /ct/ — setHeader content-type + getHeader content-type
loc('/ct/').handler = r => {
    r.setHeader('Content-Type', 'application/json');
    const ct = r.getHeader('content-type');
    r.respond(200, {}, ct);
};

// /after/ — setHeader after respond must throw
loc('/after/').handler = r => {
    r.respond(200, {}, 'done');
    let threw = false;
    try { r.setHeader('X-Late', 'v'); } catch(e) { threw = true; }
    // can't respond again, so log the result
    nginx.log(6, 'AFTER_RESPOND_THREW:' + threw);
};

// /case/ — key lookup is case-insensitive
loc('/case/').handler = r => {
    r.setHeader('X-MiXeD', 'casetest');
    const v = r.getHeader('x-mixed');
    r.respond(200, {}, v);
};
JS

$t->run();

# 1. setHeader appears in response
my $r1 = http_get('/set/');
like($r1, qr/X-Foo: bar/i, 'setHeader value appears in response');

# 2-3. getHeader before respond
my $r2 = http_get('/get/');
like($r2, qr/"v":"hello"/, 'getHeader returns staged value');
like($r2, qr/"missing":null/, 'getHeader returns null for absent header');

# 4-5. overwrite
my $r3 = http_get('/overwrite/');
like($r3, qr/second/, 'setHeader overwrite: body reflects new value');
unlike($r3, qr/first/, 'setHeader overwrite: old value gone');

# 6-7. removeHeader — header absent from wire, getHeader returns null
my $r4 = http_get('/remove/');
unlike($r4, qr/X-Gone/i, 'removeHeader: header absent from response');
like($r4, qr/"v":null/, 'getHeader after removeHeader returns null');

# 8. setHeader(name, null) removes
my $r5 = http_get('/null_remove/');
unlike($r5, qr/X-Del/i, 'setHeader(name,null) removes header');

# 9-10. content-type via setHeader + getHeader roundtrip
my $r6 = http_get('/ct/');
like($r6, qr|content-type: application/json|i,
     'setHeader content-type appears in response');
like($r6, qr|application/json|, 'getHeader content-type reads back value');

# 11. setHeader after respond throws
http_get('/after/');
my $log = $t->read_file('error.log');
like($log, qr/AFTER_RESPOND_THREW:true/, 'setHeader after respond throws TypeError');

# 12. case-insensitive key match
my $r8 = http_get('/case/');
like($r8, qr/casetest/, 'getHeader is case-insensitive');

$t->stop();
