#!/usr/bin/perl

# Tests for std and os module availability in all three JS runtimes:
#   js_preprocess    — parse-time, top-level
#   js_init_http     — parse-time, http{} level
#   js_include       — runtime, worker scripts

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

# 2 tests per scenario × 3 scenarios = 6 explicit
# + 2 auto-checks × 3 instances = 6 auto
plan tests => 12;


# -----------------------------------------------------------------------
# Scenario 1: std and os importable in js_preprocess
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_preprocess %%TESTDIR%%/preprocess_std.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    server {
        listen      127.0.0.1:8080;
        server_name preprocess;
        location /  { return 200 "preprocess ok"; }
    }
}
EOF

    $t->write_file('preprocess_std.js', <<'JS');
import * as std from 'std';
import * as os  from 'os';

if (typeof std.open   !== 'function') throw new Error('std.open missing');
if (typeof os.getcwd  !== 'function') throw new Error('os.getcwd missing');
JS

    $t->run();

    like(http_get('/'), qr/200 OK/,          'preprocess: nginx starts with std/os import');
    like(http_get('/'), qr/preprocess ok/,   'preprocess: response body correct');
}


# -----------------------------------------------------------------------
# Scenario 2: std and os importable in js_init_http
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name init_http;
        location /  { return 200 "init_http ok"; }
    }

    js_init_http %%TESTDIR%%/init_http_std.js;
}
EOF

    $t->write_file('init_http_std.js', <<'JS');
import * as std from 'std';
import * as os  from 'os';

if (typeof std.loadFile !== 'function') throw new Error('std.loadFile missing');
if (typeof os.getcwd   !== 'function') throw new Error('os.getcwd missing');
JS

    $t->run();

    like(http_get('/'), qr/200 OK/,        'init_http: nginx starts with std/os import');
    like(http_get('/'), qr/init_http ok/,  'init_http: response body correct');
}


# -----------------------------------------------------------------------
# Scenario 3: std and os importable in js_include (worker runtime)
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_include %%TESTDIR%%/include_std.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    server {
        listen      127.0.0.1:8080;
        server_name include;
        location /  { return 200 "include ok"; }
    }
}
EOF

    $t->write_file('include_std.js', <<'JS');
import * as std from 'std';
import * as os  from 'os';

if (typeof std.open  !== 'function') throw new Error('std.open missing');
if (typeof os.getcwd !== 'function') throw new Error('os.getcwd missing');
JS

    $t->run();

    like(http_get('/'), qr/200 OK/,        'include: nginx starts with std/os import');
    like(http_get('/'), qr/include ok/,    'include: response body correct');
}
