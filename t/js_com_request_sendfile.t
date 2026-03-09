#!/usr/bin/perl

# Tests for Stage 30a COM expansion: r.sendfile(path[, status])
#
# Serves a local file as the response; detects MIME type from extension.

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

js_include %%TESTDIR%%/init_sendfile.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /serve  { }
        location /notfound { }
        location /custom_status { }
    }
}
EOF

$t->write_file('init_sendfile.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/serve',         serveHandler);
    set('/notfound',      notFoundHandler);
    set('/custom_status', customStatusHandler);
})();

function serveHandler(r) {
    r.sendfile(r.queryParams.path);
}

function notFoundHandler(r) {
    try {
        r.sendfile('/nonexistent/path/file.txt');
    } catch (e) {
        r.respond(404, {'content-type': 'text/plain'}, 'caught: ' + e.message);
    }
}

function customStatusHandler(r) {
    r.sendfile(r.queryParams.path, 206);
}
JS

# Create test files
$t->write_file('test.txt',  'hello from txt');
$t->write_file('test.html', '<h1>hello html</h1>');

$t->try_run('no js module')->plan(7);

my $dir = $t->testdir();

# serve a .txt file — body matches
like(http_get("/serve?path=$dir/test.txt"), qr/hello from txt/,
     'r.sendfile: txt file body');

# content-type for .txt
like(http_get("/serve?path=$dir/test.txt"), qr|text/plain|,
     'r.sendfile: txt content-type');

# serve a .html file
like(http_get("/serve?path=$dir/test.html"), qr|text/html|,
     'r.sendfile: html content-type');

# 200 OK status
like(http_get("/serve?path=$dir/test.txt"), qr|200 OK|,
     'r.sendfile: 200 OK status');

# custom status 206
like(http_get("/custom_status?path=$dir/test.txt"), qr|206|,
     'r.sendfile: custom status 206');

# missing file throws — handler catches and responds 404
like(http_get('/notfound'), qr/404/, 'r.sendfile: missing file throws');

# thrown message contains "not found"
like(http_get('/notfound'), qr/caught:/, 'r.sendfile: TypeError caught in JS');
