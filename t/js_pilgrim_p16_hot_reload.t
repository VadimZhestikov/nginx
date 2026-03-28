#!/usr/bin/perl

# Tests for JS-Pilgrim P16: post-fork plugin loading (nginx.use hot reload).
#
# nginx.use(path[, config]) is now callable from worker request handlers.
# When called from a worker:
#   1. The plugin is loaded immediately into that worker's runtime.
#   2. The worker sends NGX_CMD_JS_USE_PLUGIN to the master.
#   3. The master forwards it as NGX_CMD_JS_LOAD_PLUGIN to all other workers.
#   4. Each receiving worker loads the same plugin.
#
# Tests:
#  - Master-phase nginx.use (P7 behaviour, unchanged) still works.
#  - Worker nginx.use loads the plugin in the calling worker (same-worker test).
#  - Worker nginx.use broadcasts to other workers (multi-worker test).
#  - nginx.use with config object passes pluginConfig to the plugin.
#  - nginx.use with a package.json custom entry point.
#  - Repeated nginx.use calls are idempotent (no crash).

use warnings;
use strict;
use Test::More;
use POSIX ();

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(18);

# Create plugin subdirectories
my $d = $t->testdir();
for my $sub (qw(plugins plugins/static plugins/dynamic plugins/pkg)) {
    mkdir "$d/$sub" or die "mkdir $d/$sub: $!" unless -d "$d/$sub";
}

# -----------------------------------------------------------------------
# Static plugin: installed at init-time (P7 / master phase)
# -----------------------------------------------------------------------

$t->write_file('plugins/static/index.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === '/static/') {
            locs[i].handler = function(r) {
                r.respond(200, {}, 'static:' + (nginx.pluginConfig.tag || 'none') + '\n');
            };
        }
    }
})();
JS

# -----------------------------------------------------------------------
# Dynamic plugin: loaded at request time from /load/
# Registers a handler on /dynamic/ and records its config.tag.
# -----------------------------------------------------------------------

$t->write_file('plugins/dynamic/index.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var tag   = (nginx.pluginConfig && nginx.pluginConfig.tag) || 'no-tag';
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === '/dynamic/') {
            locs[i].handler = function(r) {
                r.respond(200, {}, 'dynamic:' + tag + '\n');
            };
        }
    }
})();
JS

# -----------------------------------------------------------------------
# Package-with-custom-entry plugin
# -----------------------------------------------------------------------

$t->write_file('plugins/pkg/package.json', '{"ngxjs":{"main":"plugin.js"}}');
$t->write_file('plugins/pkg/plugin.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === '/pkg/') {
            locs[i].handler = function(r) {
                r.respond(200, {}, 'pkg:ok\n');
            };
        }
    }
})();
JS

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /static/   { }
        location /dynamic/  { }
        location /pkg/      { }
        location /load/     { }
        location /load_cfg/ { }
        location /load_pkg/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* P7: master-phase plugin load */
    nginx.use('%%TESTDIR%%/plugins/static', {tag: 'master'});

    /* /load/ — worker-phase nginx.use, no config */
    by['/load/'].handler = async function(r) {
        nginx.use('%%TESTDIR%%/plugins/dynamic');
        r.respond(200, {}, 'loaded\n');
    };

    /* /load_cfg/ — worker-phase nginx.use with config */
    by['/load_cfg/'].handler = async function(r) {
        nginx.use('%%TESTDIR%%/plugins/dynamic', {tag: 'worker-cfg'});
        r.respond(200, {}, 'loaded-cfg\n');
    };

    /* /load_pkg/ — worker-phase nginx.use with package.json custom entry */
    by['/load_pkg/'].handler = async function(r) {
        nginx.use('%%TESTDIR%%/plugins/pkg');
        r.respond(200, {}, 'loaded-pkg\n');
    };
})();
JS

# Replace %%TESTDIR%% in init.js after the file is written
{
    my $dir  = $t->testdir();
    my $file = "$dir/init.js";
    open(my $fh, '<', $file) or die "open: $!";
    my $content = do { local $/; <$fh> };
    close $fh;
    $content =~ s/%%TESTDIR%%/$dir/g;
    open($fh, '>', $file) or die "open: $!";
    print $fh $content;
    close $fh;
}

$t->run();

# -----------------------------------------------------------------------
# 1–2: P7 static plugin (master-phase) still works
# -----------------------------------------------------------------------

my $r = http_get('/static/');
like($r, qr{200 OK},       'static: 200 OK');
like($r, qr{static:master}m, 'static: master-phase plugin active');

# -----------------------------------------------------------------------
# 3–4: Worker nginx.use loads dynamic plugin in calling worker
# -----------------------------------------------------------------------

$r = http_get('/load/');
like($r, qr{200 OK},  'load: trigger worker nginx.use');
like($r, qr{loaded}m, 'load: trigger response correct');

# Give the broadcast a moment to propagate
select(undef, undef, undef, 0.2);

$r = http_get('/dynamic/');
like($r, qr{200 OK},          'dynamic: 200 OK after load');
like($r, qr{dynamic:no-tag}m, 'dynamic: plugin active, no config tag');

# -----------------------------------------------------------------------
# 5–8: Worker nginx.use with config
# -----------------------------------------------------------------------

$r = http_get('/load_cfg/');
like($r, qr{200 OK},      'load_cfg: trigger');
like($r, qr{loaded-cfg}m, 'load_cfg: response correct');

select(undef, undef, undef, 0.2);

$r = http_get('/dynamic/');
like($r, qr{200 OK},             'dynamic cfg: 200 OK');
like($r, qr{dynamic:worker-cfg}m, 'dynamic cfg: pluginConfig.tag propagated');

# -----------------------------------------------------------------------
# 9–10: Worker nginx.use with package.json custom entry
# -----------------------------------------------------------------------

$r = http_get('/load_pkg/');
like($r, qr{200 OK},      'load_pkg: trigger');
like($r, qr{loaded-pkg}m, 'load_pkg: response correct');

select(undef, undef, undef, 0.2);

$r = http_get('/pkg/');
like($r, qr{200 OK},    'pkg: 200 OK after load');
like($r, qr{pkg:ok}m,   'pkg: custom entry point from package.json used');

# -----------------------------------------------------------------------
# 11–12: Repeated nginx.use call is harmless
# -----------------------------------------------------------------------

$r = http_get('/load/');
like($r, qr{200 OK}, 'repeat load: no crash');
$r = http_get('/dynamic/');
like($r, qr{200 OK}, 'repeat load: handler still serves');

# -----------------------------------------------------------------------
# Auto-checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
