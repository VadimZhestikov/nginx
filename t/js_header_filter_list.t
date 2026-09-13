#!/usr/bin/perl

# Tests for headerFilters getter and getHeaderFilter (Stage 54 G)

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

        location /meta/ { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* /meta/ — two named filters; inspect via getHeaderFilter and headerFilters */
    by['/meta/'].addHeaderFilter(function(r) {}, { name: 'f1', priority: 5  });
    by['/meta/'].addHeaderFilter(function(r) {}, { name: 'f2', priority: 20 });

    var got  = by['/meta/'].getHeaderFilter('f1');
    var none = by['/meta/'].getHeaderFilter('missing');
    var list = by['/meta/'].headerFilters;

    by['/meta/'].handler = function(r) {
        r.respond(200, {
            'X-Got-Name':  String(got  ? got.name     : 'null'),
            'X-Got-Prio':  String(got  ? got.priority : 'null'),
            'X-None':      String(none === null ? 'null' : none),
            'X-List-Len':  String(list.length),
            'X-List-0':    String(list[0].name),
            'X-List-1':    String(list[1].name),
        }, 'ok');
    };
})();
JS

$t->run();

sub hdr { my ($resp, $n) = @_; $resp =~ /^$n:\s*(.+)\r$/mi ? $1 : undef }

my $r = http_get('/meta/');

is(hdr($r, 'X-Got-Name'), 'f1',   'getHeaderFilter: returns correct name');
is(hdr($r, 'X-Got-Prio'), '5',    'getHeaderFilter: returns correct priority');
is(hdr($r, 'X-None'),     'null', 'getHeaderFilter: returns null for unknown name');
is(hdr($r, 'X-List-Len'), '2',    'headerFilters: correct length');
is(hdr($r, 'X-List-0'),   'f1',   'headerFilters: first entry is lowest priority');
is(hdr($r, 'X-List-1'),   'f2',   'headerFilters: second entry is higher priority');

