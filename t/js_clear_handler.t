#!/usr/bin/perl

# Tests for location.clearHandler():
#   loc.clearHandler()  — removes the JS content handler and restores the
#                         original clcf->handler that was in place before the
#                         first location.handler = fn assignment.
#
# Scenarios:
#   /empty/    — NULL original handler; set JS handler, clear → 404
#   /static/   — static-file handler (NULL, falls through to phase handler);
#                set JS handler → clears → static file served again
#   /noop/     — clearHandler() called when no handler was ever set → no crash
#   /recycle/  — set → clear → set again → handler works again

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        root %%TESTDIR%%;

        location /empty/   { }
        location /static/  { }
        location /noop/    { }
        location /recycle/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    var locs  = nginx.http.servers[0].locations;
    var find  = function (p) {
        return locs.find(function (l) { return l.path === p; });
    };

    /* /empty/: set handler, then clear → original NULL handler restored */
    var empty = find('/empty/');
    empty.handler = function (req) {
        req.respond(200, {}, 'from-js');
    };
    empty.clearHandler();

    /* /static/: static file location; set handler then clear → file served */
    var st = find('/static/');
    st.handler = function (req) {
        req.respond(200, {}, 'from-js');
    };
    st.clearHandler();

    /* /noop/: clearHandler() on a location that never had a JS handler */
    find('/noop/').clearHandler();

    /* /recycle/: set → clear → set again → handler must work */
    var rec = find('/recycle/');
    rec.handler = function (req) { req.respond(200, {}, 'first'); };
    rec.clearHandler();
    rec.handler = function (req) { req.respond(200, {}, 'second'); };
})();
JS

# Static file served by /static/ location after clearHandler
mkdir $t->testdir() . '/static';
$t->write_file('static/index.html', 'static-content');

$t->try_run('no js module')->plan(9);

# /empty/: JS handler was cleared → nginx has no content handler → 404
my $r_empty = http_get('/empty/');
like($r_empty, qr/404/, 'empty: clearHandler restores null → 404');

# /static/: clearHandler restores null → static module serves the file
my $r_static = http_get('/static/');
like($r_static, qr/200/, 'static: clearHandler → 200 from static module');
like($r_static, qr/static-content/, 'static: clearHandler → correct file content');

# /noop/: no crash, no JS handler → 404
my $r_noop = http_get('/noop/');
like($r_noop, qr/[34]\d\d/, 'noop: clearHandler on unset location → no crash');
unlike($r_noop, qr/from-js/, 'noop: no js response body');

# /recycle/: set → clear → set → second handler fires
my $r_rec = http_get('/recycle/');
like($r_rec, qr/200/, 'recycle: second handler → 200');
like($r_rec, qr/second/, 'recycle: second handler body');
unlike($r_rec, qr/first/, 'recycle: first handler not active');

# Original handler restored correctly across requests
like(http_get('/static/'), qr/static-content/, 'static: second request still serves file');

$t->stop();
