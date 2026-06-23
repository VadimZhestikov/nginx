#!/usr/bin/perl

# Tests for object-or-key ergonomics (follow-up #3).
#
# removeLocation / restoreLocation, removeServer / restoreServer and
# removeListener / restoreListener accept EITHER the key string they always
# took OR the COM object returned by the matching add*/createSocket — so
# `x = addX(); removeX(x)` works as well as `removeX("key")`.  The object's key
# property is pulled off it: location → .pattern, server → .name, socket → .address.
#
# This test drives the OBJECT form and asserts it has the same effect as the
# string form (route/vhost/listener toggles), via a /ctl/ request handler.
# worker_processes 1 makes the single-worker effect deterministic.

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
        location /ctl/  { }
        location /gone/ { }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  vhosta;
        location /who/ { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
var LADDR = '127.0.0.1:%%PORT_8081%%';
var sock  = nginx.createSocket(LADDR);
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

nginx.broadcast(function () {
    var srv = nginx.http.servers[0];

    srv.locations.find(function (l) { return l.path === '/gone/'; }).handler =
        function (r) { r.respond(200, {}, 'GONE\n'); };

    var va = nginx.http.servers.find(function (s) { return s.name === 'vhosta'; });
    if (va) {
        var w = va.locations.find(function (l) { return l.path === '/who/'; });
        if (w) { w.handler = function (r) { r.respond(200, {}, 'vhosta\n'); }; }
    }

    /*
     * Capture the COM objects ONCE (as a real caller would: x = addX(); …;
     * removeX(x)).  After a soft remove the location is no longer in the live
     * .locations list, so re-finding it would fail — the saved reference is the
     * whole point of the object form.
     */
    var savedLoc = srv.locations.find(function (l) { return l.path === '/gone/'; });
    var savedSrv = va;

    srv.locations.find(function (l) { return l.path === '/ctl/'; }).handler =
    function (r) {
        var q = r.queryParams, out = {};
        try {
            /* every branch passes the COM OBJECT, not the key string */
            if      (q.op === 'rmLocObj')   { out.r = srv.removeLocation(savedLoc); }
            else if (q.op === 'restLocObj') { out.r = srv.restoreLocation(savedLoc); }
            else if (q.op === 'rmSrvObj')   { out.r = nginx.http.removeServer(savedSrv); }
            else if (q.op === 'restSrvObj') { out.r = nginx.http.restoreServer(savedSrv); }
            else if (q.op === 'rmLsnObj')   { out.r = nginx.http.removeListener(sock); }
            else if (q.op === 'restLsnObj') { out.r = nginx.http.restoreListener(sock); }
        } catch (e) { out.err = String(e.message); }
        r.respond(200, {'Content-Type':'application/json'}, JSON.stringify(out) + '\n');
    };
});
JS

$t->try_run('no js module')->plan(15);

my $lport = port(8081);

sub code  { my $r = http_get(shift); return $1 if $r =~ m!^HTTP/\d\.\d\s+(\d+)!; 0 }
sub jbody { my $r = http_get(shift); $r =~ s/.*?\r\n\r\n//s; $r }
sub who {  # server_name that answered /who/ for a given Host
    my $r = http("GET /who/ HTTP/1.0\r\nHost: $_[0]\r\n\r\n");
    return $1 if $r =~ /\r\n\r\n(\w+)/;
    return $r =~ m!\s(\d{3})\s! ? "code$1" : 'NORESP';
}
sub probe {  # 'PONG'/'NORESP'/'CONNFAIL' for the second listener
    my $s = IO::Socket::INET->new(PeerAddr => "127.0.0.1:$lport", Timeout => 2);
    return 'CONNFAIL' unless $s;
    $s->print("GET /gone/ HTTP/1.0\r\nHost: localhost\r\n\r\n");
    my $rin = ''; vec($rin, fileno($s), 1) = 1;
    my $n = select($rin, undef, undef, 1.5);
    if (!$n) { close $s; return 'NORESP'; }
    my $buf = ''; sysread($s, $buf, 1024); close $s;
    return $buf =~ /GONE/ ? 'PONG' : 'OTHER';
}

# ── baseline ─────────────────────────────────────────────────────────────────
is(code('/gone/'), 200,        'location /gone/ live initially');
is(who('vhosta'),  'vhosta',   'vhosta serves initially');
is(probe(),        'PONG',     'second listener :8081 serves initially');

# ── removeLocation / restoreLocation by OBJECT ───────────────────────────────
like(jbody('/ctl/?op=rmLocObj'), qr/"r":true/, 'removeLocation(object) returns true');
is(code('/gone/'), 404,        '/gone/ removed by object (404)');
like(jbody('/ctl/?op=restLocObj'), qr/"r":true/, 'restoreLocation(object) returns true');
is(code('/gone/'), 200,        '/gone/ restored by object (200)');

# ── removeServer / restoreServer by OBJECT ───────────────────────────────────
like(jbody('/ctl/?op=rmSrvObj'), qr/"r":true/, 'removeServer(object) returns true');
isnt(who('vhosta'), 'vhosta',  'vhosta removed by object (falls through)');
like(jbody('/ctl/?op=restSrvObj'), qr/"r":true/, 'restoreServer(object) returns true');
is(who('vhosta'),  'vhosta',   'vhosta restored by object');

# ── removeListener / restoreListener by OBJECT (the createSocket handle) ──────
like(jbody('/ctl/?op=rmLsnObj'), qr/"r":true/, 'removeListener(object) returns true');
is(probe(),        'NORESP',   ':8081 paused by socket object');
like(jbody('/ctl/?op=restLsnObj'), qr/"r":true/, 'restoreListener(object) returns true');
is(probe(),        'PONG',     ':8081 resumed by socket object');
