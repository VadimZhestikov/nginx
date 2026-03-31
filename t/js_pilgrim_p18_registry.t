#!/usr/bin/perl

# Tests for P18: Online package registry for nginx.use
#
# nginx.use('vendor/plugin@version') resolves a versioned package reference
# from the local on-disk cache ($NGXJS_CACHE/packages/vendor/plugin/version/).
# The runtime itself never touches the network — ngxjs install populates the
# cache offline.
#
# Cache layout used by tests:
#   $testdir/cache/
#     packages/
#       acme/
#         hello/
#           1.0.0/
#             index.js        ← handler for /hello/ → 'hello-1.0.0'
#         greet/
#           2.3.1/
#             package.json    ← { "ngxjs": { "main": "greet.js" } }
#             greet.js        ← handler for /greet/ → 'greet-2.3.1'
#           .latest           ← "2.3.1"
#
# Tests:
#   1-2   nginx.use('acme/hello@1.0.0') loads from cache — 200 + correct body
#   3-4   nginx.use('acme/greet') (no @version) uses .latest → 2.3.1
#   5-6   package.json ngxjs.main entry point honoured (greet.js, not index.js)
#   7     nginx starts cleanly — no crash from registry-ref loading
#   8-9   no file-read errors logged for successfully loaded packages
#  10-11  missing package → error logged with "ngxjs install" hint
#  12     chain: both plugins remain accessible after loading

use warnings;
use strict;
use Test::More;
use File::Path qw(make_path);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

# -----------------------------------------------------------------------
# Build the fake package cache in the test directory
# -----------------------------------------------------------------------

my $cache = $t->testdir() . '/cache';

# acme/hello@1.0.0 — minimal plugin, no package.json
make_path("$cache/packages/acme/hello/1.0.0");
$t->write_file('cache/packages/acme/hello/1.0.0/index.js', <<'JS');
(function() {
    var loc = nginx.http.servers[0].locations.find(function(l) {
        return l.path === '/hello/';
    });
    if (loc) {
        loc.handler = function(req) {
            req.respond(200, {'Content-Type': 'text/plain'}, 'hello-1.0.0\n');
        };
    }
}());
JS

# acme/greet@2.3.1 — uses package.json ngxjs.main
make_path("$cache/packages/acme/greet/2.3.1");
$t->write_file('cache/packages/acme/greet/2.3.1/package.json',
    '{"name":"acme/greet","ngxjs":{"main":"greet.js"}}');
$t->write_file('cache/packages/acme/greet/2.3.1/greet.js', <<'JS');
(function() {
    var loc = nginx.http.servers[0].locations.find(function(l) {
        return l.path === '/greet/';
    });
    if (loc) {
        loc.handler = function(req) {
            req.respond(200, {'Content-Type': 'text/plain'}, 'greet-2.3.1\n');
        };
    }
}());
JS
$t->write_file('cache/packages/acme/greet/.latest', "2.3.1\n");

# -----------------------------------------------------------------------
# nginx.conf
# -----------------------------------------------------------------------

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

        location /hello/  { }
        location /greet/  { }
        location /status/ { }
    }
}
EOF

# init.js:
#  - loads both plugins via registry refs
#  - catches the missing-package exception and logs it for test 10-11
#  - sets up /status/ handler
$t->write_file('init.js', <<'JS');
/* P18: registry references resolved via NGXJS_CACHE env */

/* tests 1-2: explicit version */
nginx.use('acme/hello@1.0.0');

/* tests 3-6: version-less ref -> .latest -> 2.3.1 (also tests ngxjs.main) */
nginx.use('acme/greet');

/* tests 10-11: missing package — catch so nginx still starts */
try {
    nginx.use('acme/nosuchpkg@9.9.9');
} catch (e) {
    nginx.log(6, 'PKGERR: ' + e);
}

/* test 7: status handler proves nginx started cleanly */
(function() {
    var loc = nginx.http.servers[0].locations.find(function(l) {
        return l.path === '/status/';
    });
    if (loc) {
        loc.handler = function(req) {
            req.respond(200, {'Content-Type': 'text/plain'}, 'ok\n');
        };
    }
}());
JS

# Set NGXJS_CACHE: the master process inherits this at init_conf time.
# No nginx `env` directive needed — nginx.use() runs in the master which
# already has the full parent-process environment.
$ENV{NGXJS_CACHE} = $cache;

$t->try_run('no js module')->plan(12);

# -----------------------------------------------------------------------
# 1-2: nginx.use('acme/hello@1.0.0') loads plugin from cache
# -----------------------------------------------------------------------

my $r = http_get('/hello/');
like($r, qr{200 OK},         'hello: 200 OK');
like($r, qr{hello-1\.0\.0}, 'hello: correct plugin response');

# -----------------------------------------------------------------------
# 3-4: nginx.use('acme/greet') — no @version — uses .latest file
# -----------------------------------------------------------------------

$r = http_get('/greet/');
like($r, qr{200 OK},         'greet: 200 OK (resolved via .latest)');
like($r, qr{greet-2\.3\.1}, 'greet: correct body — .latest resolved to 2.3.1');

# -----------------------------------------------------------------------
# 5-6: package.json ngxjs.main entry point honoured
# greet.js emits "greet-2.3.1"; default index.js would not exist here.
# -----------------------------------------------------------------------

like($r, qr{greet},   'ngxjs-main: greet.js entry point used');
like($r, qr{2\.3\.1}, 'ngxjs-main: version from .latest is 2.3.1');

# -----------------------------------------------------------------------
# 7: nginx started cleanly — no crash from registry-ref loading
# -----------------------------------------------------------------------

$r = http_get('/status/');
like($r, qr{200 OK}, 'startup: nginx started cleanly');

# -----------------------------------------------------------------------
# 8-9: no file-read errors in log for successfully loaded packages
# -----------------------------------------------------------------------

my $log = $t->read_file('error.log');
unlike($log, qr{failed to read.*acme/hello},
    'no-err: acme/hello@1.0.0 loaded without file errors');
unlike($log, qr{failed to read.*acme/greet},
    'no-err: acme/greet@2.3.1 loaded without file errors');

# -----------------------------------------------------------------------
# 10-11: missing package — error log contains helpful "ngxjs install" hint
# -----------------------------------------------------------------------

like($log, qr{not installed.*acme/nosuchpkg|acme/nosuchpkg.*not installed},
    'missing-pkg: "not installed" in error log');
like($log, qr{ngxjs install},
    'missing-pkg: "ngxjs install" suggested in error log');

# -----------------------------------------------------------------------
# 12: chain — both plugins still accessible after loading both
# -----------------------------------------------------------------------

$r = http_get('/hello/');
like($r, qr{hello-1\.0\.0}, 'chain: acme/hello@1.0.0 still works after chaining');

$t->stop();
