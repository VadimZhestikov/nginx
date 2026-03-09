#!/usr/bin/perl

# Tests for Stage 21 COM expansion: r.log(level, message)
#
# r.log(level, message) writes to the nginx error log using the request's
# connection log context (includes client address).
# Supported levels: "debug", "info", "warn", "error".

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
error_log  %%TESTDIR%%/error.log info;

js_include %%TESTDIR%%/init_request_log.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /log_error {
        }

        location /log_warn {
        }

        location /log_info {
        }

        location /log_unknown {
        }
    }
}
EOF

$t->write_file('init_request_log.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/log_error',   logErrorHandler);
    set('/log_warn',    logWarnHandler);
    set('/log_info',    logInfoHandler);
    set('/log_unknown', logUnknownHandler);
})();

function logErrorHandler(r) {
    r.log('error', 'stage21-error-marker');
    r.respond(200, {'content-type': 'text/plain'}, 'logged error');
}

function logWarnHandler(r) {
    r.log('warn', 'stage21-warn-marker');
    r.respond(200, {'content-type': 'text/plain'}, 'logged warn');
}

function logInfoHandler(r) {
    r.log('info', 'stage21-info-marker');
    r.respond(200, {'content-type': 'text/plain'}, 'logged info');
}

function logUnknownHandler(r) {
    /* unknown level defaults to error */
    r.log('unknown', 'stage21-unknown-marker');
    r.respond(200, {'content-type': 'text/plain'}, 'logged unknown');
}
JS

$t->try_run('no js module')->plan(8);

# Each handler responds 200 so we can tell it ran
like(http_get('/log_error'),   qr/200 OK/, 'log error: handler responds 200');
like(http_get('/log_warn'),    qr/200 OK/, 'log warn: handler responds 200');
like(http_get('/log_info'),    qr/200 OK/, 'log info: handler responds 200');
like(http_get('/log_unknown'), qr/200 OK/, 'log unknown level: handler responds 200');

# Verify messages actually appear in the error log
my $log = $t->read_file('error.log');

like($log, qr/js: stage21-error-marker/,   'log error: message in error.log');
like($log, qr/js: stage21-warn-marker/,    'log warn: message in error.log');
like($log, qr/js: stage21-info-marker/,    'log info: message in error.log');
like($log, qr/js: stage21-unknown-marker/, 'log unknown: falls back to error level');
