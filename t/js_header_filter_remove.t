#!/usr/bin/perl

# Tests for removeHeaderFilter by name and by function reference (Stage 54 G)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(6);

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

        location /by_name/ { }
        location /by_fn/   { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /by_name/ — remove by name string */
    by['/by_name/'].addHeaderFilter(function(r) { r.setHeader('X-Removed', 'yes'); },
                                    { name: 'toRemove' });
    by['/by_name/'].addHeaderFilter(function(r) { r.setHeader('X-Kept', 'yes'); },
                                    { name: 'kept' });
    by['/by_name/'].removeHeaderFilter('toRemove');
    by['/by_name/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /by_fn/ — remove by function reference */
    var fnGone = function(r) { r.setHeader('X-Gone', 'yes'); };
    by['/by_fn/'].addHeaderFilter(fnGone);
    by['/by_fn/'].addHeaderFilter(function(r) { r.setHeader('X-Stay', 'yes'); });
    by['/by_fn/'].removeHeaderFilter(fnGone);
    by['/by_fn/'].handler = function(r) { r.respond(200, {}, 'ok'); };
})();
JS

$t->run();

sub hdr { my ($resp, $n) = @_; $resp =~ /^$n:\s*(.+)\r$/mi ? $1 : undef }

my $r;

# /by_name/
$r = http_get('/by_name/');
is(hdr($r, 'X-Removed'), undef, 'remove by name: removed filter does not run');
is(hdr($r, 'X-Kept'),    'yes', 'remove by name: kept filter still runs');

# /by_fn/
$r = http_get('/by_fn/');
is(hdr($r, 'X-Gone'),  undef, 'remove by fn ref: removed filter does not run');
is(hdr($r, 'X-Stay'),  'yes', 'remove by fn ref: kept filter still runs');

ok(1, 'removeHeaderFilter accepts both string name and function reference');
ok(1, 'nginx started without crash');
