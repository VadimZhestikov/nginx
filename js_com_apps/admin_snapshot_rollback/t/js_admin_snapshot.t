#!/usr/bin/perl

# Tests for admin.js createSnapshot():
#   /set_weight    — set peer weight via request handler
#   /create_snap   — call admin.createSnapshot(name), return id
#   /snap_content  — return raw snapshot JSON for the last snapshot
#   /list_snaps    — return JSON array of snapshot ids
#   /check_apply   — apply the snapshot, verify peers match
#
# Scenarios:
#   create_snap_200  — POST /create_snap returns 200 with a snapshot id
#   snap_id_format   — snapshot id starts with "0001-"
#   snap_has_peer    — snapshot JSON contains the modified peer weight
#   list_nonempty    — listSnapshots returns the newly created id
#   apply_from_snap  — applySnapshot restores weight from the snapshot

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
        server 127.0.0.2:%%PORT_8091%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /set_weight   { }
        location /create_snap  { }
        location /snap_content { }
        location /list_snaps   { }
        location /check_apply  { }
    }
}
EOF

mkdir $t->testdir() . '/snapshots';

$t->write_file('init.js', <<'JS');
import * as std from 'std';

var locs = nginx.http.servers[0].locations;
var ups  = nginx.http.upstreams.find(function (u) { return u.name === 'backend'; });

locs.find(function (l) { return l.path === '/set_weight'; }).handler =
    function (req) {
        var w = parseInt(req.queryParams.w, 10);
        if (isNaN(w)) { req.respond(400, {}, 'bad w'); return; }
        ups.peers[0].weight = w;
        req.respond(200, {'Content-Type': 'text/plain'}, 'ok');
    };

locs.find(function (l) { return l.path === '/create_snap'; }).handler =
    function (req) {
        var name = req.queryParams.name || 'test';
        try {
            var id = nginx.admin.createSnapshot(name);
            req.respond(200, {'Content-Type': 'text/plain'}, id);
        } catch (e) {
            req.respond(500, {}, String(e));
        }
    };

locs.find(function (l) { return l.path === '/snap_content'; }).handler =
    function (req) {
        var list = nginx.admin.listSnapshots();
        if (!list.length) { req.respond(404, {}, 'no snapshots'); return; }
        /* read last snapshot file via std */
        var path = nginx.cycle.prefix + 'snapshots/' + list[list.length - 1] + '.json';
        var text = std.loadFile(path);
        if (!text) { req.respond(500, {}, 'cannot read ' + path); return; }
        req.respond(200, {'Content-Type': 'application/json'}, text);
    };

locs.find(function (l) { return l.path === '/list_snaps'; }).handler =
    function (req) {
        var list = nginx.admin.listSnapshots();
        req.respond(200, {'Content-Type': 'application/json'},
                    JSON.stringify(list));
    };

locs.find(function (l) { return l.path === '/check_apply'; }).handler =
    function (req) {
        /* Reset weight to 1, then re-apply snapshot; check weight is 7 */
        ups.peers[0].weight = 1;
        var list = nginx.admin.listSnapshots();
        if (!list.length) { req.respond(404, {}, 'no snapshots'); return; }
        try {
            nginx.admin.applySnapshot(list[list.length - 1]);
        } catch (e) {
            req.respond(500, {}, String(e)); return;
        }
        req.respond(200, {'Content-Type': 'text/plain'},
                    String(ups.peers[0].weight));
    };
JS

$t->try_run('no js module')->plan(5);

# 1. Set peer weight to 7
http("GET /set_weight?w=7 HTTP/1.0\r\nHost: localhost\r\n\r\n");

# 2. Create snapshot
my $r_create = http("POST /create_snap?name=weight-7 HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_create, qr/200/, 'create_snap_200: createSnapshot returns 200');

# Extract snapshot id from response body
my ($snap_id) = ($r_create =~ /\r\n\r\n(.*)/s);
$snap_id =~ s/\s+$//;
like($snap_id, qr/^0001-/, 'snap_id_format: id starts with 0001-');

# 3. Verify snapshot JSON contains the weight
my $r_content = http("GET /snap_content HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_content, qr/weight.*\b7\b|\b7\b.*weight/s, 'snap_has_peer: snapshot records weight 7');

# 4. List snapshots contains the new id
my $r_list = http("GET /list_snaps HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_list, qr/0001-weight-7/, 'list_nonempty: listSnapshots includes new snapshot');

# 5. Apply snapshot re-applies weight 7
my $r_apply = http("GET /check_apply HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_apply, qr/\b7\b/, 'apply_from_snap: applySnapshot restores weight 7');

$t->stop();
