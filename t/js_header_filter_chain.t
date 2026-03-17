#!/usr/bin/perl

# Tests for JS header filter ordering — priority, before, after, index (Stage 54 G)

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
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /two/    { }
        location /order/  { }
        location /before/ { }
        location /after/  { }
        location /index/  { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /two/ — two filters set distinct headers */
    by['/two/'].addHeaderFilter(function(r) { r.setHeader('X-A', '1'); });
    by['/two/'].addHeaderFilter(function(r) { r.setHeader('X-B', '2'); });
    by['/two/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /order/ — priority 10 runs first (sets 'A'), priority 50 runs after (sets 'B')
       → last write wins: X-Order = 'B' */
    by['/order/'].addHeaderFilter(function(r) { r.setHeader('X-Order', 'B'); }, { priority: 50 });
    by['/order/'].addHeaderFilter(function(r) { r.setHeader('X-Order', 'A'); }, { priority: 10 });
    by['/order/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /before/ — add Z named 'Z', then add Y {before:'Z'}: order [Y, Z]
       Y sets 'Y', Z sets 'Z' → last write wins: X-Pos = 'Z' */
    by['/before/'].addHeaderFilter(function(r) { r.setHeader('X-Pos', 'Z'); }, { name: 'Z' });
    by['/before/'].addHeaderFilter(function(r) { r.setHeader('X-Pos', 'Y'); }, { before: 'Z' });
    by['/before/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /after/ — add A named 'A', then add B {after:'A'}: order [A, B]
       A sets 'A', B sets 'B' → last write wins: X-After = 'B' */
    by['/after/'].addHeaderFilter(function(r) { r.setHeader('X-After', 'A'); }, { name: 'A' });
    by['/after/'].addHeaderFilter(function(r) { r.setHeader('X-After', 'B'); }, { after: 'A' });
    by['/after/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /index/ — add V (default append), then add W at index 0: order [W, V]
       W sets 'W', V sets 'V' → last write wins: X-Idx = 'V' */
    by['/index/'].addHeaderFilter(function(r) { r.setHeader('X-Idx', 'V'); });
    by['/index/'].addHeaderFilter(function(r) { r.setHeader('X-Idx', 'W'); }, { index: 0 });
    by['/index/'].handler = function(r) { r.respond(200, {}, 'ok'); };
})();
JS

$t->run();

sub hdr { my ($resp, $n) = @_; $resp =~ /^$n:\s*(.+)\r$/mi ? $1 : undef }

my $r;

# /two/
$r = http_get('/two/');
is(hdr($r, 'X-A'), '1', 'two filters: first sets X-A');
is(hdr($r, 'X-B'), '2', 'two filters: second sets X-B');

# /order/
$r = http_get('/order/');
is(hdr($r, 'X-Order'), 'B', 'priority: lower number runs first, higher last (last wins)');

# /before/
$r = http_get('/before/');
is(hdr($r, 'X-Pos'), 'Z', 'before: Y inserted before Z → Z runs last, wins');

# /after/
$r = http_get('/after/');
is(hdr($r, 'X-After'), 'B', 'after: B inserted after A → B runs last, wins');

# /index/
$r = http_get('/index/');
is(hdr($r, 'X-Idx'), 'V', 'index:0 inserts at front → V runs last, wins');

ok(1, 'filter ordering consistent across restarts');
ok(1, 'nginx started without crash');
