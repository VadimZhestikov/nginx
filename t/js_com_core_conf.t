#!/usr/bin/perl

# Tests for nginx.cycle core-conf properties (js_com_core_commands)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(7);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

worker_shutdown_timeout  3s;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /prop/ { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// js_com_core_commands: nginx.cycle core conf properties

var cy = nginx.cycle;

var props = {
    master:           String(cy.master),
    timerResolution:  String(cy.timerResolution),
    shutdownTimeout:  String(cy.shutdownTimeout),
    priority:         String(cy.priority),
    rlimitNofile:     String(cy.rlimitNofile),
    workingDirectory: String(cy.workingDirectory),
};

(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/prop/") {
            locs[i].handler = function(req) {
                var key = req.args.replace(/^key=/, '');
                req.respond(200, {}, props[key] !== undefined ? props[key] : "undefined");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }
sub prop { body(http_get("/prop/?key=$_[0]")) }

# daemon off is forced by Test::Nginx; master_process default is on
is(prop('master'),           'true',  'master_process defaults to true');
is(prop('timerResolution'),  '0',     'timerResolution default 0 (disabled)');
is(prop('shutdownTimeout'),  '3000',  'shutdownTimeout 3s → 3000ms');
is(prop('priority'),         '0',     'priority default 0');
is(prop('rlimitNofile'),     '-1',    'rlimitNofile unset → -1');
is(prop('workingDirectory'), '',      'workingDirectory unset → empty string');

# Verify workers and daemon are still working (regression)
is(body(http_get('/prop/?key=undefined')), 'undefined', 'unknown key → "undefined"');
