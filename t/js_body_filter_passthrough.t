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
        location /number/ { }
        location /object/ { }
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

    /* /number/ and /object/ — a filter that returns a NON-STRING.  The claim
       was that these are treated as pass-through like undefined and null; it
       rode on an ok(1) and was never exercised.  Written as two locations
       rather than one because a number and an object can fail differently: a
       number could be coerced to its decimal text, an object to
       "[object Object]", and either would show up here as a changed body. */
    by['/number/'].addBodyFilter('wholeBodySync', function(r, body) { return 42; });
    by['/number/'].handler = function(r) { r.respond(200, {}, 'num-keep'); };

    by['/object/'].addBodyFilter('wholeBodySync', function(r, body) { return { a: 1 }; });
    by['/object/'].handler = function(r) { r.respond(200, {}, 'obj-keep'); };
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

$r = http_get('/number/');
is(body($r), 'num-keep',
    'a filter returning a NUMBER is pass-through, not coerced to "42"');

$r = http_get('/object/');
is(body($r), 'obj-keep',
    'a filter returning an OBJECT is pass-through, not coerced to "[object Object]"');
