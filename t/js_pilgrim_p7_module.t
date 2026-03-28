#!/usr/bin/perl

# Tests for JS-Pilgrim P7: nginx.use() / nginx.install()
#
# nginx.use(path[, config]) — load a JS plugin from a directory on the
#   filesystem.  The directory's index.js (or the entry named in
#   package.json's ngxjs.main key) is evaluated.  nginx.pluginConfig is
#   set to the config argument before evaluation.
#
# nginx.install(plugin[, config]) — invoke an inline plugin: either a
#   function or an object with an .install method, called with config||{}.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

my $td = $t->testdir();

# Create plugin subdirectories before writing files
mkdir "$td/plugins";
mkdir "$td/plugins/pluginA";
mkdir "$td/plugins/pluginB";

# -----------------------------------------------------------------------
# Plugin A — no package.json; entry = index.js
# Sets loc[0] handler; echoes "pluginA:<config.tag>"
# -----------------------------------------------------------------------

$t->write_file_expand('plugins/pluginA/index.js', <<'JS');
var cfg = nginx.pluginConfig;
var tag = (cfg && cfg.tag) ? cfg.tag : 'none';
nginx.http.servers[0].locations[0].handler = function(r) {
    r.respond(200, {}, 'pluginA:' + tag + '\n');
};
JS

# -----------------------------------------------------------------------
# Plugin B — package.json with ngxjs.main pointing to plugin-main.js
# Sets loc[1] handler; echoes "pluginB:<config.msg>"
# -----------------------------------------------------------------------

$t->write_file_expand('plugins/pluginB/package.json', <<'JSON');
{
  "name": "pluginB",
  "ngxjs": { "main": "plugin-main.js" }
}
JSON

$t->write_file_expand('plugins/pluginB/plugin-main.js', <<'JS');
var cfg = nginx.pluginConfig;
var msg = (cfg && cfg.msg) ? cfg.msg : 'default';
nginx.http.servers[0].locations[1].handler = function(r) {
    r.respond(200, {}, 'pluginB:' + msg + '\n');
};
JS

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p7_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /a/   { }
        location /b/   { }
        location /c/   { }
        location /fn/  { }
    }
}
EOF

$t->write_file_expand('p7_init.js', <<'JS');
// JS-Pilgrim P7 — plugin packaging test init

// Plugin A: no package.json; loads index.js
nginx.use('%%TESTDIR%%/plugins/pluginA', { tag: 'hello' });

// Plugin B: package.json with ngxjs.main; loads plugin-main.js
nginx.use('%%TESTDIR%%/plugins/pluginB', { msg: 'world' });

// Inline plugin: object with .install method
nginx.install({
    install: function(cfg) {
        nginx.http.servers[0].locations[2].handler = function(r) {
            r.respond(200, {}, 'pluginC:' + cfg.val + '\n');
        };
    }
}, { val: 'obj-ok' });

// Inline plugin: bare function
nginx.install(function(cfg) {
    nginx.http.servers[0].locations[3].handler = function(r) {
        r.respond(200, {}, 'pluginFn:' + cfg.x + '\n');
    };
}, { x: 99 });
JS

$t->run();

# -----------------------------------------------------------------------
# 1–2: nginx.use with default index.js entry; config.tag echoed
# -----------------------------------------------------------------------

my $r = http_get('/a/');
like($r, qr{200 OK},         'use/indexjs: 200 OK');
like($r, qr{pluginA:hello},  'use/indexjs: config.tag visible in plugin');

# -----------------------------------------------------------------------
# 3–4: nginx.use with package.json ngxjs.main entry; config.msg echoed
# -----------------------------------------------------------------------

$r = http_get('/b/');
like($r, qr{200 OK},         'use/pkgjson: 200 OK');
like($r, qr{pluginB:world},  'use/pkgjson: config.msg visible in plugin');

# -----------------------------------------------------------------------
# 5–6: nginx.install with object plugin (.install method)
# -----------------------------------------------------------------------

$r = http_get('/c/');
like($r, qr{200 OK},          'install/obj: 200 OK');
like($r, qr{pluginC:obj-ok},  'install/obj: config.val visible in plugin');

# -----------------------------------------------------------------------
# 7–8: nginx.install with function plugin
# -----------------------------------------------------------------------

$r = http_get('/fn/');
like($r, qr{200 OK},          'install/fn: 200 OK');
like($r, qr{pluginFn:99},     'install/fn: config.x visible in plugin');

# -----------------------------------------------------------------------
# 9–10: Both plugins still active (use() chaining didn't clobber state)
# -----------------------------------------------------------------------

like(http_get('/a/'), qr{pluginA:hello}, 'chaining: plugin A still active');
like(http_get('/b/'), qr{pluginB:world}, 'chaining: plugin B still active');

# -----------------------------------------------------------------------
# 11–12: Auto checks — nginx started, no alerts
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
