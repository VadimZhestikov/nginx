#!/usr/bin/perl

# Tests for JS-Pilgrim P17: standard listen socket support for P4/P6.
#
# server.on('accept', fn)    — fires on TCP accept for standard listen sockets.
# server.addL4Filter(fn)     — raw inbound data filter on standard sockets.
# server.addL4SendFilter(fn) — raw outbound data filter on standard sockets.
#
# Tests:
#  1-2   Basic request still works with accept hooks registered.
#  3-4   Accept hook fires: module-level counter is incremented per connection.
#  5     conn.reject() closes the connection (one-shot arm via /reject_next/).
#  6     Normal requests resume immediately after the single rejection.
#  7-8   Server continues to accept normally.
#  9-10  L4 inbound filter (pass-through) does not break requests.
#  11-12 L4 send filter (pass-through) does not break requests.
#  13-14 Both accept hooks fire (second hook also counted).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(16);

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

        location /hello/        { }
        location /count/        { }
        location /count2/       { }
        location /reject_next/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function() {
    var srv = nginx.http.servers[0];
    var locs = srv.locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* Module-level state (worker-local; tests use worker_processes=1) */
    var count1     = 0;
    var count2     = 0;
    var rejectNext = false;

    /* ------------------------------------------------------------------ */
    /* Location handlers                                                    */
    /* ------------------------------------------------------------------ */

    by['/hello/'].handler = function(r) {
        r.respond(200, {}, 'hello\n');
    };

    by['/count/'].handler = function(r) {
        r.respond(200, {}, 'count1=' + count1 + '\n');
    };

    by['/count2/'].handler = function(r) {
        r.respond(200, {}, 'count2=' + count2 + '\n');
    };

    by['/reject_next/'].handler = function(r) {
        rejectNext = true;
        r.respond(200, {}, 'armed\n');
    };

    /* ------------------------------------------------------------------ */
    /* P17 accept hooks                                                     */
    /* ------------------------------------------------------------------ */

    /* Hook 1: increment count1 on every accepted connection */
    srv.on('accept', function(conn) {
        count1++;
    });

    /* Hook 2: increment count2; one-shot reject if rejectNext is set */
    srv.on('accept', function(conn) {
        count2++;
        if (rejectNext) {
            rejectNext = false;
            conn.reject();
        }
    });

    /* ------------------------------------------------------------------ */
    /* P17 L4 inbound filter (pass-through — verifies no breakage)         */
    /* ------------------------------------------------------------------ */

    srv.addL4Filter(async function*(source) {
        for await (const chunk of source) {
            yield chunk;
            return;
        }
    });

    /* ------------------------------------------------------------------ */
    /* P17 L4 send filter (pass-through — verifies no breakage)            */
    /* ------------------------------------------------------------------ */

    srv.addL4SendFilter(async function*(source) {
        for await (const chunk of source) {
            yield chunk;
        }
    });
})();
JS

$t->run();

# -----------------------------------------------------------------------
# 1-2: Basic request with hooks registered
# -----------------------------------------------------------------------

my $r = http_get('/hello/');
like($r, qr{200 OK}, 'hello: 200 OK');
like($r, qr{hello},  'hello: body correct');

# -----------------------------------------------------------------------
# 3-4: Accept hook fires — count1 incremented per connection
# -----------------------------------------------------------------------

my $c = http_get('/count/');
like($c, qr{200 OK},        'count: 200 OK');
like($c, qr{count1=[1-9]},  'count: hook1 fired (count1 >= 1)');

# -----------------------------------------------------------------------
# 5-6: conn.reject() closes the connection
# -----------------------------------------------------------------------

http_get('/reject_next/');  # arm one-shot reject

# Accept hook rejects the very next connection; http_get returns '' or undef
my $rej = http_get('/hello/');
ok(!defined($rej) || $rej !~ /200 OK/, 'rejected: no 200 response');

# rejectNext auto-cleared on rejection — no explicit reset needed
like(http_get('/hello/'), qr{200 OK}, 'after reject: normal requests resume');

# -----------------------------------------------------------------------
# 7-8: Server accepts normally after unblocking
# -----------------------------------------------------------------------

my $r2 = http_get('/hello/');
like($r2, qr{200 OK}, 'after unblock: 200 OK');
like($r2, qr{hello},  'after unblock: body correct');

# -----------------------------------------------------------------------
# 9-10: L4 inbound filter (pass-through) does not break requests
# -----------------------------------------------------------------------

my $r3 = http_get('/hello/');
like($r3, qr{200 OK}, 'l4 filter: 200 OK');
like($r3, qr{hello},  'l4 filter: body correct');

# -----------------------------------------------------------------------
# 11-12: L4 send filter (pass-through) does not break requests
# -----------------------------------------------------------------------

my $r4 = http_get('/hello/');
like($r4, qr{200 OK}, 'send filter: 200 OK');
like($r4, qr{hello},  'send filter: body correct');

# -----------------------------------------------------------------------
# 13-14: Both accept hooks fire (count2 also incremented)
# -----------------------------------------------------------------------

my $c2 = http_get('/count2/');
like($c2, qr{200 OK},       'count2: 200 OK');
like($c2, qr{count2=[1-9]}, 'count2: hook2 also fired');

# -----------------------------------------------------------------------
# Auto-checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
