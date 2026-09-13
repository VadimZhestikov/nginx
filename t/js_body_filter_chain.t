#!/usr/bin/perl

# Tests for JS body filter chaining — multiple filters compose (Stage 54 G)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(3);

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

        location /chain/  { }
        location /order/  { }
        location /emptymid/ { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /chain/ — first filter appends '!', second wraps in brackets
       input: 'hi' → 'hi!' → '[hi!]' */
    by['/chain/'].addBodyFilter('wholeBodySync', function(r, body) { return body + '!'; });
    by['/chain/'].addBodyFilter('wholeBodySync', function(r, body) { return '[' + body + ']'; });
    by['/chain/'].handler = function(r) { r.respond(200, {}, 'hi'); };

    /* /order/ — priority 10 runs first (prepends 'A:'), priority 50 runs after (prepends 'B:')
       input: 'x' → 'A:x' → 'B:A:x' */
    by['/order/'].addBodyFilter('wholeBodySync', function(r, body) { return 'B:' + body; }, { priority: 50 });
    by['/order/'].addBodyFilter('wholeBodySync', function(r, body) { return 'A:' + body; }, { priority: 10 });
    by['/order/'].handler = function(r) { r.respond(200, {}, 'x'); };

    /* /emptymid/ — the first filter empties the body, the second must STILL
       run and be able to produce output from it.  A chain that stops when an
       intermediate result is empty would return '' here instead of 'refilled',
       and the claim "chained filters all run even if intermediate body is
       empty" used to be an ok(1) with nothing behind it. */
    by['/emptymid/'].addBodyFilter('wholeBodySync',
        function(r, body) { return ''; }, { priority: 10 });
    by['/emptymid/'].addBodyFilter('wholeBodySync',
        function(r, body) { return body === '' ? 'refilled' : 'NOT-EMPTY:' + body; },
        { priority: 50 });
    by['/emptymid/'].handler = function(r) { r.respond(200, {}, 'original'); };
})();
JS

$t->run();

sub body { my ($resp) = @_; $resp =~ /\r\n\r\n(.*)/s ? $1 : undef }

my $r;

$r = http_get('/chain/');
is(body($r), '[hi!]', 'two body filters compose in registration order');

$r = http_get('/order/');
is(body($r), 'B:A:x', 'priority: lower number runs first, output feeds next filter');

$r = http_get('/emptymid/');
is(body($r), 'refilled',
    'chained filters all run even if an intermediate body is empty: the second '
    . 'filter saw the first\'s empty result and produced output from it');
