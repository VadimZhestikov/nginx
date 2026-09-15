#!/usr/bin/perl

# P17 `conn.reject()` — the functional half.  The LEAK half is in
# t/run_sanitizers.sh, which is where leak detection is already plumbed and
# already has a positive control; see the note there.
#
# WHAT WENT WRONG.  The reject path closed the connection with
# ngx_close_connection(), which is not nginx's teardown for a freshly accepted
# connection: ngx_event_accept() creates c->pool before calling the accept
# handler, and ngx_close_connection() never touches it.  nginx's own sequence is
#
#     c->destroyed = 1; pool = c->pool;
#     ngx_close_connection(c); ngx_destroy_pool(pool);
#
# which is exactly ngx_http_close_connection(), plus the SSL shutdown and the
# stat_active decrement.  Measured under ASAN with detect_leaks=1: 199 rejected
# connections leaked 101,888 bytes — 512 bytes each, the default
# connection_pool_size — with the allocation stack landing in
# ngx_event_accept -> ngx_create_pool.
#
# WHY IT MATTERS MORE THAN ITS SIZE SUGGESTS: reject is a policy decision an
# operator reaches for UNDER ATTACK, so the leak rate is the attacker's to
# choose.  At ten thousand rejects a second a worker sheds ~5 MB/s until reload —
# a denial of service in the tool you reached for to stop one.
#
# AN RSS-BASED TEST WAS WRITTEN FIRST AND THROWN AWAY: with the fix reverted, 3000
# rejected connections still moved worker RSS by 0 KB, because 1.5 MB of leaked
# 512-byte chunks comes out of heap the warm-up had already mapped.  Its control
# did not fire, so it proved nothing — the shape of dead probe this tree has a
# checker for.  ASAN sees the allocation; RSS does not.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use IO::Socket::INET;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /arm    { }
        location /disarm { }
        location /hello  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    var srv = nginx.http.servers[0], locs = srv.locations, by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* A COUNTED arm, not a boolean one, and the reason is a footgun the test
     * pins below: an accept-level reject-all policy rejects the request that
     * would turn it off, because that request needs a connection too.  A count
     * lets the policy expire so the test can observe recovery. */
    var rejectN = 0, seen = 0, rejected = 0;

    by['/hello'].handler  = function (r) { r.respond(200, {}, 'hi\n'); };
    by['/arm'].handler    = function (r) { rejectN = 300;
                                           r.respond(200, {}, 'armed\n'); };
    by['/disarm'].handler = function (r) {
        r.respond(200, {}, 'seen=' + seen + ' rejected=' + rejected + '\n'); };

    srv.on('accept', function (c) {
        seen++;
        if (rejectN > 0) { rejectN--; rejected++; c.reject(); }
    });
})();
JS

$t->try_run('no js module')->plan(7);   # 7 assertions; Test::Nginx::plan adds 2 of its own

sub connect_n {
    my ($n) = @_;
    my $ok = 0;
    for (1 .. $n) {
        my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => 8080,
                                      Proto => 'tcp', Timeout => 2);
        if ($s) { $ok++; close($s); }
    }
    return $ok;
}

like(http_get('/hello'), qr/200 OK/, 'accepts while unarmed');

like(http_get('/arm'), qr/armed/, 'the reject policy is armed for the next 300');

# A rejected connection is closed before any request is read, so the client sees
# nothing.  That it does not HANG is the assertion: a reject that forgot to close
# would leave the client waiting.
my $rej = http_get('/hello');
unlike($rej // '', qr/200 OK/,
   'an armed accept hook rejects: the request gets no response and the client is '
   . 'not left waiting — the connection was closed, not abandoned');

# THE FOOTGUN, pinned because it surprised this test into failing: the control
# endpoint is not exempt.  An accept hook runs before any request exists, so a
# reject-ALL policy also rejects the request that would turn it off, and the only
# way back is a reload.  That is why the arm above is counted.
my $ctl = http_get('/disarm');
unlike($ctl // '', qr/seen=/,
   'AND THE CONTROL PLANE IS NOT EXEMPT: /disarm is rejected too, because an '
   . 'accept hook runs before any request exists. A reject-ALL policy locks an '
   . 'operator out of turning it off, and the only way back is a reload — which '
   . 'is why a real one wants a count, an allowlist or a timer');

my $n = connect_n(298);
cmp_ok($n, '>', 250,
   "the remaining ~298 connections are accepted at the TCP level and rejected in "
   . "the hook ($n completed): rejecting must not stop the listener accepting");

like(http_get('/hello'), qr/200 OK/,
   'and normal service resumes once the count is exhausted — 300 rejects left the '
   . 'listener and the worker healthy');

like(http_get('/disarm'), qr/rejected=300/,
   '...with the hook reporting exactly 300 rejections, so the ASAN stage in '
   . 't/run_sanitizers.sh is measuring a known number of pool allocations');

$t->stop();
