#!/usr/bin/perl

# Tests for admin.js rollback() and pin():
#
# Scenarios (request-phase):
#   /set_weight    — set peer[0] weight via query param
#   /snap          — createSnapshot, return id
#   /rollback      — admin.rollback(), return previous snapshot id or "base"
#   /get_weight    — return current peer[0] weight
#   /pin           — admin.pin(id) via query param, return ok
#
# Test flow:
#   1. Set weight=5, create snap "snap-5"    → id 0001-snap-5
#   2. Set weight=9, create snap "snap-9"    → id 0002-snap-9
#   3. rollback → apply 0001-snap-5, weight == 5
#   4. rollback again → apply base, weight == 1
#   5. rollback with no more history → still at base, weight == 1
#   6. pin(0002-snap-9), then rollback → apply 0001-snap-5, weight == 5

use warnings;
use strict;
use Test::More;
use File::Spec;
use Cwd qw(abs_path);

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $admin_js = abs_path(
    File::Spec->catfile($FindBin::Bin, '..', 'conf', 'admin.js')
);

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_source $admin_js;
js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:%%PORT_8091%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /set_weight { }
        location /snap       { }
        location /rollback   { }
        location /get_weight { }
        location /pin        { }
    }
}
EOF

mkdir $t->testdir() . '/snapshots';

$t->write_file('init.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var ups  = nginx.http.upstreams.find(function (u) { return u.name === 'backend'; });

locs.find(function (l) { return l.path === '/set_weight'; }).handler =
    function (req) {
        var w = parseInt(req.queryParams.w, 10);
        if (isNaN(w)) { req.respond(400, {}, 'bad w'); return; }
        ups.peers[0].weight = w;
        req.respond(200, {}, 'ok');
    };

locs.find(function (l) { return l.path === '/snap'; }).handler =
    function (req) {
        var name = req.queryParams.name || 'snap';
        try {
            req.respond(200, {'Content-Type': 'text/plain'},
                        nginx.admin.createSnapshot(name));
        } catch (e) {
            req.respond(500, {}, String(e));
        }
    };

locs.find(function (l) { return l.path === '/rollback'; }).handler =
    function (req) {
        try {
            var prev = nginx.admin.rollback();
            req.respond(200, {'Content-Type': 'text/plain'}, prev || 'base');
        } catch (e) {
            req.respond(500, {}, String(e));
        }
    };

locs.find(function (l) { return l.path === '/get_weight'; }).handler =
    function (req) {
        req.respond(200, {'Content-Type': 'text/plain'},
                    String(ups.peers[0].weight));
    };

locs.find(function (l) { return l.path === '/pin'; }).handler =
    function (req) {
        var id = req.queryParams.id || null;
        try {
            nginx.admin.pin(id);
            req.respond(200, {}, 'ok');
        } catch (e) {
            req.respond(500, {}, String(e));
        }
    };
JS

$t->try_run('no js module')->plan(8);

# 1. Create two snapshots at different weights
http("GET /set_weight?w=5 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my $r1 = http("POST /snap?name=snap-5 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my ($id1) = ($r1 =~ /\r\n\r\n(.*)/s); $id1 =~ s/\s+$//;

http("GET /set_weight?w=9 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my $r2 = http("POST /snap?name=snap-9 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my ($id2) = ($r2 =~ /\r\n\r\n(.*)/s); $id2 =~ s/\s+$//;

like($id1, qr/^0001-snap-5/, 'snap-5 id is 0001-snap-5');
like($id2, qr/^0002-snap-9/, 'snap-9 id is 0002-snap-9');

# 2. First rollback: pinned = snap-9 → previous = snap-5, weight becomes 5
my $r_rb1 = http("POST /rollback HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_rb1, qr/0001-snap-5/, 'first rollback reverts to snap-5');
my $r_w1 = http("GET /get_weight HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_w1, qr/\b5\b/, 'peer weight is 5 after first rollback');

# 3. Second rollback: pinned = snap-5 → previous = null → base, weight becomes 1
my $r_rb2 = http("POST /rollback HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_rb2, qr/base/, 'second rollback reverts to base state');
my $r_w2 = http("GET /get_weight HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_w2, qr/\b1\b/, 'peer weight is 1 after base rollback');

# 4. pin(snap-9) then rollback → should go to snap-5
http("POST /pin?id=$id2 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my $r_rb3 = http("POST /rollback HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_rb3, qr/0001-snap-5/, 'rollback after pin(snap-9) reverts to snap-5');
my $r_w3 = http("GET /get_weight HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_w3, qr/\b5\b/, 'peer weight is 5 after rollback from pinned snap-9');

$t->stop();
