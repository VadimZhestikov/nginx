#!/usr/bin/perl

# Tests for Stage 30b COM expansion: r.redirect(url[, code])
#
# Sends an HTTP redirect with a Location header and empty body.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_redirect.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /redir_302   { }
        location /redir_301   { }
        location /redir_bad   { }
    }
}
EOF

$t->write_file('init_redirect.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/redir_302', redir302);
    set('/redir_301', redir301);
    set('/redir_bad', redirBad);
})();

function redir302(r) {
    r.redirect('/target');
}

function redir301(r) {
    r.redirect('https://example.com/page', 301);
}

function redirBad(r) {
    try {
        r.redirect('/x', 200);  /* 200 is not 3xx */
        r.respond(200, {'content-type': 'text/plain'}, 'no error');
    } catch (e) {
        r.respond(400, {'content-type': 'text/plain'}, 'caught: ' + e.message);
    }
}
JS

$t->try_run('no js module')->plan(7);

# default 302
like(http_get('/redir_302'), qr|302|, 'r.redirect: default 302 status');
like(http_get('/redir_302'), qr|Location:.*\Q/target\E|,
     'r.redirect: Location header contains /target');

# explicit 301
like(http_get('/redir_301'), qr|301|, 'r.redirect: explicit 301');
like(http_get('/redir_301'), qr|Location: https://example\.com/page|,
     'r.redirect: absolute URL in Location');

# non-3xx status throws
like(http_get('/redir_bad'), qr|caught:|, 'r.redirect: non-3xx throws TypeError');

# body of a redirect is empty
my $r302 = http_get('/redir_302');
like($r302, qr/\r\n\r\n$/, 'r.redirect: empty body');

# verify both status line and location in one response
like($r302, qr|302.*Location|s, 'r.redirect: 302 + Location in response');
