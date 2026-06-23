#!/usr/bin/perl

# Tests for reversible removeServer (tombstone) + restoreServer (Track S).
#
# removeServer(name) now soft-removes by default: the server's names are dropped
# from the virtual-server hash (requests fall through to the default server) but
# the cscf + JS wrapper are kept, so restoreServer(name) brings it back with
# identity preserved. removeServer(name, {hard:true}) keeps the legacy
# irreversible splice. Both take effect immediately (auto rebuildVhostDispatch).

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
        listen       127.0.0.1:8080 default_server;
        server_name  def;
        location /who/ { }
        location /ctl/ { }
    }
    server {
        listen       127.0.0.1:8080;
        server_name  vhostA;
        location /who/ { }
    }
    server {
        listen       127.0.0.1:8080;
        server_name  vhostB;
        location /who/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
nginx.broadcast(function () {
    nginx.http.servers.forEach(function (s) {
        s.locations.forEach(function (l) {
            if (l.path === '/who/') {
                l.handler = (function (nm) {
                    return function (r) { r.respond(200, {}, nm + '\n'); };
                })(s.name);
            }
        });
    });
    /* control endpoint lives on the default server */
    var def = nginx.http.servers.find(function (s) { return s.name === 'def'; });
    def.locations.find(function (l) { return l.path === '/ctl/'; }).handler =
    function (r) {
        var q = r.queryParams, out = {};
        try {
            if (q.op === 'remove')      { out.r = nginx.http.removeServer(q.name); }
            else if (q.op === 'restore'){ out.r = nginx.http.restoreServer(q.name); }
            else if (q.op === 'hard')   { out.r = nginx.http.removeServer(q.name, {hard:true}); }
        } catch (e) { out.err = String(e.message); }
        r.respond(200, {'Content-Type':'application/json'}, JSON.stringify(out) + '\n');
    };
});
JS

$t->try_run('no js module')->plan(13);

# who($host) returns the server_name that served /who/ for that Host header.
# http_get hardcodes Host: localhost, so build the raw request with our Host.
sub req {
    my ($path, $host) = @_;
    my $r = http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
    $r =~ s/.*?\r\n\r\n//s; $r =~ s/\s+$//;
    return $r;
}
sub who { req('/who/', $_[0]) }
sub ctl { req("/ctl/?$_[0]", 'def') }

# ── baseline: each vhost routes to itself ────────────────────────────────────
is(who('vhostA'), 'vhosta', 'vhostA routes to itself initially');
is(who('vhostB'), 'vhostb', 'vhostB routes to itself initially');

# ── soft remove vhostA → falls through to default; restore brings it back ────
like(ctl('op=remove&name=vhostA'), qr/"r":true/, 'removeServer vhostA returns true');
is(who('vhostA'), 'def',    'after soft remove: vhostA falls through to default');
is(who('vhostB'), 'vhostb', 'sibling vhostB unaffected');
like(ctl('op=restore&name=vhostA'), qr/"r":true/, 'restoreServer vhostA returns true');
is(who('vhostA'), 'vhosta', 'after restore: vhostA routes again (identity preserved)');

# ── many cycles stay correct ─────────────────────────────────────────────────
my $cycle_ok = 1;
for (1 .. 30) {
    ctl('op=remove&name=vhostB');
    ctl('op=restore&name=vhostB');
}
is(who('vhostB'), 'vhostb', 'vhostB live after 30 remove/restore cycles');

# ── restoring a non-removed server is a no-op (false) ────────────────────────
like(ctl('op=restore&name=vhostB'), qr/"r":false/, 'restore of live server returns false');

# ── removing an unknown server → false ───────────────────────────────────────
like(ctl('op=remove&name=nope'), qr/"r":false/, 'removeServer unknown returns false');

# ── hard remove is irreversible ──────────────────────────────────────────────
like(ctl('op=hard&name=vhostA'), qr/"r":true/, 'hard removeServer vhostA');
is(who('vhostA'), 'def', 'after hard remove: vhostA falls through (immediate)');
like(ctl('op=restore&name=vhostA'), qr/"r":false/,
     'restore after hard remove returns false (wrapper spliced, gone)');
