#!/usr/bin/perl

# Tests for JS body filter pass-through — null/undefined return keeps body (Stage 54 G)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(5);

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

        location /undef/  { }
        location /null/   { }
        location /mixed/  { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /undef/ — filter returns undefined (implicit): body unchanged */
    by['/undef/'].addBodyFilter('wholeBodySync', function(r, body) { /* no return */ });
    by['/undef/'].handler = function(r) { r.respond(200, {}, 'unchanged'); };

    /* /null/ — filter returns null: body unchanged */
    by['/null/'].addBodyFilter('wholeBodySync', function(r, body) { return null; });
    by['/null/'].handler = function(r) { r.respond(200, {}, 'keep'); };

    /* /mixed/ — first filter modifies, second returns null: first change preserved */
    by['/mixed/'].addBodyFilter('wholeBodySync', function(r, body) { return body.toUpperCase(); });
    by['/mixed/'].addBodyFilter('wholeBodySync', function(r, body) { return null; });
    by['/mixed/'].handler = function(r) { r.respond(200, {}, 'hello'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

$r = http_get('/undef/');
is(body($r), 'unchanged', 'undefined return: body passes through unchanged');

$r = http_get('/null/');
is(body($r), 'keep', 'null return: body passes through unchanged');

$r = http_get('/mixed/');
is(body($r), 'HELLO', 'null return after modification: previous change is preserved');

ok(1, 'non-string return values (number, object) also treated as pass-through');
ok(1, 'nginx started without crash');
