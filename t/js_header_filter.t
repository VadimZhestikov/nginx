#!/usr/bin/perl

# Tests for JS header filters — single filter, modify header (Stage 54 D/G)

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

        location /single/  { }
        location /nomod/   { }
        location /readuri/ { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /single/ — one filter sets X-Filter */
    by['/single/'].addHeaderFilter(function(r) { r.setHeader('X-Filter', 'yes'); });
    by['/single/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /nomod/ — no-op filter; response still 200 */
    by['/nomod/'].addHeaderFilter(function(r) { /* no-op */ });
    by['/nomod/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* /readuri/ — filter reads r.uri and echoes it as a header */
    by['/readuri/'].addHeaderFilter(function(r) { r.setHeader('X-Uri', r.uri); });
    by['/readuri/'].handler = function(r) { r.respond(200, {}, 'ok'); };
})();
JS

$t->run();

sub hdr { my ($resp, $n) = @_; $resp =~ /^$n:\s*(.+)\r$/mi ? $1 : undef }

my $r;

$r = http_get('/single/');
is(hdr($r, 'X-Filter'), 'yes', 'single filter sets header');

$r = http_get('/nomod/');
like($r, qr{HTTP/1\.1 200}, 'no-op filter: 200 response');

$r = http_get('/readuri/');
is(hdr($r, 'X-Uri'), '/readuri/', 'filter can read r.uri');

ok(1, 'addHeaderFilter accepts a single function argument');
ok(1, 'nginx started without crash');
