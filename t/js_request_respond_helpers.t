#!/usr/bin/perl

# Stage 42: r.json(data[, status]), r.text(str[, status]), r.html(str[, status])
#
# Convenience response methods that set Content-Type and send a complete
# response in one call.
#
#   r.json(data[, status])   JSON.stringify(data) + application/json
#   r.text(str[, status])    String(str)          + text/plain
#   r.html(str[, status])    String(str)          + text/html
#
# Tests:
#   1.  r.json({}) sends Content-Type: application/json
#   2.  r.json body is valid JSON
#   3.  r.json default status is 200
#   4.  r.json(data, 201) sends 201
#   5.  r.text("hello") sends Content-Type: text/plain
#   6.  r.text body is the string value
#   7.  r.text default status is 200
#   8.  r.text(str, 404) sends 404
#   9.  r.html("<b>x</b>") sends Content-Type: text/html
#  10.  r.html body is the html string
#  11.  r.json with staged statusCode uses it as default
#  12.  r.json(array) stringifies an array correctly

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

        location /json/        { }
        location /json_status/ { }
        location /text/        { }
        location /text_status/ { }
        location /html/        { }
        location /staged/      { }
        location /array/       { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const s = nginx.http.servers[0];
const loc = p => s.locations.find(l => l.path === p);

loc('/json/').handler = r => {
    r.json({ ok: true, value: 42 });
};

loc('/json_status/').handler = r => {
    r.json({ error: 'not found' }, 404);
};

loc('/text/').handler = r => {
    r.text('hello world');
};

loc('/text_status/').handler = r => {
    r.text('gone', 410);
};

loc('/html/').handler = r => {
    r.html('<b>bold</b>');
};

loc('/staged/').handler = r => {
    r.statusCode = 202;
    r.json({ staged: true });
};

loc('/array/').handler = r => {
    r.json([1, 2, 3]);
};
JS

$t->run();

# 1-3: r.json basic
my $r1 = http_get('/json/');
like($r1, qr|content-type: application/json|i, 'r.json sets application/json');
like($r1, qr/\{"ok":true,"value":42\}/,         'r.json body is JSON-encoded object');
like($r1, qr|HTTP/1.1 200|,                      'r.json default status 200');

# 4: r.json with explicit status
my $r2 = http_get('/json_status/');
like($r2, qr|HTTP/1.1 404|, 'r.json(data, 404) sends 404');

# 5-7: r.text basic
my $r3 = http_get('/text/');
like($r3, qr|content-type: text/plain|i, 'r.text sets text/plain');
like($r3, qr/hello world/,               'r.text body is the string');
like($r3, qr|HTTP/1.1 200|,              'r.text default status 200');

# 8: r.text with explicit status
my $r4 = http_get('/text_status/');
like($r4, qr|HTTP/1.1 410|, 'r.text(str, 410) sends 410');

# 9-10: r.html
my $r5 = http_get('/html/');
like($r5, qr|content-type: text/html|i, 'r.html sets text/html');
like($r5, qr|<b>bold</b>|,              'r.html body is the html string');

# 11: staged statusCode used by r.json
my $r6 = http_get('/staged/');
like($r6, qr|HTTP/1.1 202|, 'r.json uses staged r.statusCode as default');

# 12: array stringified correctly
my $r7 = http_get('/array/');
like($r7, qr/\[1,2,3\]/, 'r.json(array) stringifies array');

$t->stop();
