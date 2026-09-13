#!/usr/bin/perl

# Tests for filter dispatch reentrancy safety.
#
# A filter callback may call addHeaderFilter / removeHeaderFilter (or the body
# equivalents) on its own location while the filter chain is dispatching.
# Expected behaviour (matches browser event-listener semantics):
#
#   - Filter ADDED during dispatch: not run in the current request; runs from
#     the next request onward (it is in the live list by then).
#   - Filter REMOVED during dispatch before it has run: skipped in the current
#     request (its fn_idx was unregistered; JS_IsFunction returns false).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(9);

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

        location /hdr_add/    { }
        location /hdr_remove/ { }
        location /body_add/   { }
        location /body_remove/{ }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
(function() {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* ------------------------------------------------------------------ */
    /* /hdr_add/ — filter A adds filter B during first dispatch.
     * First request:  snapshot has [A] only → X-B absent.
     * Second request: snapshot has [A, B]   → X-B present.          */
    var hdrAdded = false;
    by['/hdr_add/'].addHeaderFilter(function(r) {
        r.setHeader('X-A', 'ran');
        if (!hdrAdded) {
            hdrAdded = true;
            r.location.addHeaderFilter(function(r2) {
                r2.setHeader('X-B', 'ran');
            });
        }
    });
    by['/hdr_add/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* ------------------------------------------------------------------ */
    /* /hdr_remove/ — filter A (priority 10) removes filter B (priority 20)
     * before B has had a chance to run.
     * B's fn_idx is unregistered → JS_IsFunction returns false → skipped. */
    by['/hdr_remove/'].addHeaderFilter(function(r) {
        r.setHeader('X-A', 'ran');
        r.location.removeHeaderFilter('toRemove');
    }, { priority: 10 });
    by['/hdr_remove/'].addHeaderFilter(function(r) {
        r.setHeader('X-B', 'ran');
    }, { name: 'toRemove', priority: 20 });
    by['/hdr_remove/'].handler = function(r) { r.respond(200, {}, 'ok'); };

    /* ------------------------------------------------------------------ */
    /* /body_add/ — body filter A adds body filter B during first dispatch.
     * First request:  snapshot has [A] only → body = 'HI'  (no '!').
     * Second request: snapshot has [A, B]   → body = 'HI!' (B appends). */
    var bodyAdded = false;
    by['/body_add/'].addBodyFilter('wholeBodySync', function(r, body) {
        if (!bodyAdded) {
            bodyAdded = true;
            r.location.addBodyFilter('wholeBodySync', function(r2, b) { return b + '!'; });
        }
        return body.toUpperCase();
    });
    by['/body_add/'].handler = function(r) { r.respond(200, {}, 'hi'); };

    /* ------------------------------------------------------------------ */
    /* /body_remove/ — body filter A (priority 10) removes filter B
     * (priority 20) before B runs.  B is skipped → body = 'x:A'. */
    by['/body_remove/'].addBodyFilter('wholeBodySync', function(r, body) {
        r.location.removeBodyFilter('late');
        return body + ':A';
    }, { priority: 10 });
    by['/body_remove/'].addBodyFilter('wholeBodySync', function(r, body) {
        return body + ':B';
    }, { name: 'late', priority: 20 });
    by['/body_remove/'].handler = function(r) { r.respond(200, {}, 'x'); };
})();
JS

$t->run();

sub hdr  { my ($resp, $n) = @_; $resp =~ /^$n:\s*(.+)\r$/mi ? $1 : undef }
sub body { my ($resp)     = @_; $resp =~ /\r\n\r\n(.*)/s    ? $1 : undef }

my $r;

# /hdr_add/ — first request: B not yet in snapshot
$r = http_get('/hdr_add/');
is(hdr($r, 'X-A'), 'ran', 'hdr_add first req: A ran');
is(hdr($r, 'X-B'), undef, 'hdr_add first req: B not in snapshot, did not run');

# /hdr_add/ — second request: B is now in the live list
$r = http_get('/hdr_add/');
is(hdr($r, 'X-A'), 'ran', 'hdr_add second req: A still runs');
is(hdr($r, 'X-B'), 'ran', 'hdr_add second req: B now in list, runs');

# /hdr_remove/ — B removed before it fires
$r = http_get('/hdr_remove/');
is(hdr($r, 'X-A'), 'ran',  'hdr_remove: A ran');
is(hdr($r, 'X-B'), undef,  'hdr_remove: B removed before it ran, skipped');

# /body_add/ — first request: B not yet in snapshot
$r = http_get('/body_add/');
is(body($r), 'HI',  'body_add first req: only A ran (no exclamation)');

# /body_add/ — second request: B is now in the live list
$r = http_get('/body_add/');
is(body($r), 'HI!', 'body_add second req: A then B ran');

# /body_remove/ — B removed before it fires
$r = http_get('/body_remove/');
is(body($r), 'x:A', 'body_remove: B removed before it ran, only A applied');

