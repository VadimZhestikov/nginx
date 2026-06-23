#!/usr/bin/perl

# Tests for reversible removeListener / restoreListener (Track N, soft pause).
#
# removeListener(addr) soft-pauses: ngx_del_event removes the worker's accept
# event so it stops accepting on that listener (the fd stays bound, so new
# connections complete the TCP handshake but get no HTTP response — they queue).
# restoreListener(addr) re-arms the accept event. {hard:true} (close/refuse) is
# reserved (needs master-side close) and must throw.
#
# A second listener is created at init_conf via createSocket()+attach(); the
# control server (8080) stays up so we can drive remove/restore and never lose
# the channel. worker_processes 1 makes the single-worker pause deterministic.

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

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

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location /ping/ { }
        location /ctl/  { }
    }
}
EOF

# init.js is expanded so %%PORT_8081%% becomes the allocated second port.
$t->write_file_expand('init.js', <<'EOF');
// Create a second listener on an allocated port and attach it to the server.
var LADDR = '127.0.0.1:%%PORT_8081%%';
var sock  = nginx.createSocket(LADDR);
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

nginx.broadcast(function () {
    var s = nginx.http.servers[0];
    s.locations.find(function (l) { return l.path === '/ping/'; }).handler =
        function (r) { r.respond(200, {}, 'pong\n'); };

    s.locations.find(function (l) { return l.path === '/ctl/'; }).handler =
        function (r) {
            var q = r.queryParams, out = {};
            try {
                if (q.op === 'remove')      { out.r = nginx.http.removeListener(LADDR); }
                else if (q.op === 'restore'){ out.r = nginx.http.restoreListener(LADDR); }
                else if (q.op === 'hard')   { out.r = nginx.http.removeListener(LADDR, {hard:true}); }
            } catch (e) { out.err = String(e.message); }
            r.respond(200, {'Content-Type':'application/json'}, JSON.stringify(out) + '\n');
        };
});
EOF

$t->try_run('no js module')->plan(11);

my $lport = port(8081);   # the second listener's allocated port

# probe($port) → 'PONG' (served), 'NORESP' (accepting paused), or 'CONNFAIL'.
sub probe {
    my $port = shift;
    my $s = IO::Socket::INET->new(PeerAddr => "127.0.0.1:$port", Timeout => 2);
    return 'CONNFAIL' unless $s;
    $s->print("GET /ping/ HTTP/1.0\r\nHost: localhost\r\n\r\n");
    my $rin = '';
    vec($rin, fileno($s), 1) = 1;
    my $n = select($rin, undef, undef, 1.5);
    if (!$n) { close $s; return 'NORESP'; }
    my $buf = '';
    sysread($s, $buf, 1024);
    close $s;
    return $buf =~ /pong/ ? 'PONG' : 'OTHER';
}
sub ctl { my $r = http_get("/ctl/?op=$_[0]"); $r =~ s/.*?\r\n\r\n//s; $r }

# ── baseline: both the control port and the second listener serve ────────────
like(http_get('/ping/'), qr/pong/,           'control :8080 serves');
is(probe($lport), 'PONG',                     'second listener serves initially');

# ── soft removeListener → second listener stops responding (queues) ──────────
like(ctl('remove'), qr/"r":true/,             'removeListener returns true');
is(probe($lport), 'NORESP',                   'paused listener: no HTTP response');
like(http_get('/ping/'), qr/pong/,            'control port unaffected by pause');

# ── restoreListener → serves again ───────────────────────────────────────────
like(ctl('restore'), qr/"r":true/,            'restoreListener returns true');
is(probe($lport), 'PONG',                     'restored listener serves again');

# ── many cycles stay correct ─────────────────────────────────────────────────
for (1 .. 20) { ctl('remove'); ctl('restore'); }
is(probe($lport), 'PONG',                     'listener serves after 20 pause/restore cycles');

# ── idempotence: double remove then restore ──────────────────────────────────
ctl('remove'); ctl('remove');
is(probe($lport), 'NORESP',                   'double remove still paused');
ctl('restore');
is(probe($lport), 'PONG',                     'restore after double remove serves');

# ── {hard:true} is reserved (must throw) ─────────────────────────────────────
like(ctl('hard'), qr/not yet supported/,      'hard removeListener is reserved (throws)');
