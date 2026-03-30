#!/usr/bin/perl

# Edge-case tests for admin.js + admin-api.js.
#
# Covers scenarios NOT in js_admin_base.t / js_admin_api.t:
#   1-2   ops-format snapshot applied correctly (weight changed via ops)
#   3     rollback() from the first (only) snapshot resets to base
#   4-5   POST /admin/apply/nonexistent → 404 + JSON error body
#   6-7   GET  /admin/snapshots/nonexistent → 404 + JSON error body
#   8     Unknown admin route → 404 JSON error
#   9-10  POST /admin/snapshots with invalid JSON body → 400
#  11-12  POST /admin/snapshots without name → 400
#  13-14  pin(null) clears pin; next rollback goes to most recent snapshot
#  15-16  listSnapshots() returns sorted order (0001 before 0002)

use warnings;
use strict;
use Test::More;
use File::Spec;
use Cwd qw(abs_path);
use JSON::PP;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib '../../../t/lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

my $admin_js     = abs_path(File::Spec->catfile($FindBin::Bin, '..', 'conf', 'admin.js'));
my $admin_api_js = abs_path(File::Spec->catfile($FindBin::Bin, '..', 'conf', 'admin-api.js'));

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_source $admin_js;
js_source $admin_api_js;
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

        location /admin/ { }
        location /weight { }
        location /pin    { }
        location /rb     { }
    }
}
EOF

mkdir $t->testdir() . '/snapshots';

# Pre-seed an ops-format snapshot (no legacy peers/handlers keys)
$t->write_file('snapshots/0001-ops-snap.json', encode_json({
    id  => '0001-ops-snap',
    ts  => 1,
    ops => [
        { path => 'http.upstreams[0].peers[0].weight', value => 9 },
    ],
}));

$t->write_file('init.js', <<'JS');
/* /weight: GET → current weight; ?w=N → set weight */
var ups = nginx.http.upstreams.find(function (u) { return u.name === 'backend'; });

var wloc = nginx.http.servers[0].locations.find(function (l) {
    return l.path === '/weight';
});
wloc.handler = function (req) {
    var w = req.queryParams.w;
    if (w !== undefined) {
        ups.peers[0].weight = parseInt(w, 10);
        req.respond(200, {}, 'ok');
    } else {
        req.respond(200, {'Content-Type': 'text/plain'},
                    'weight=' + String(ups.peers[0].weight));
    }
};

/* /pin?id=X — call nginx.admin.pin(id); id='null' means pin(null) */
var pinloc = nginx.http.servers[0].locations.find(function (l) {
    return l.path === '/pin';
});
pinloc.handler = function (req) {
    var id = req.queryParams.id;
    try {
        nginx.admin.pin(id === 'null' ? null : id);
        req.respond(200, {}, 'ok');
    } catch (e) {
        req.respond(500, {'Content-Type': 'application/json'},
                    JSON.stringify({error: String(e.message || e)}) + '\n');
    }
};

/* /rb — call nginx.admin.rollback() directly; returns JSON */
var rbloc = nginx.http.servers[0].locations.find(function (l) {
    return l.path === '/rb';
});
rbloc.handler = function (req) {
    try {
        var prev = nginx.admin.rollback();
        req.respond(200, {'Content-Type': 'application/json'},
                    JSON.stringify({rolledBackTo: prev || 'base'}) + '\n');
    } catch (e) {
        req.respond(500, {'Content-Type': 'application/json'},
                    JSON.stringify({error: String(e.message || e)}) + '\n');
    }
};
JS

$t->try_run('no js module')->plan(16);

# -----------------------------------------------------------------------
# 1-2: ops-format snapshot applied correctly
# The pre-seeded 0001-ops-snap.json uses the {ops:[...]} format.
# Applying it must set peers[0].weight to 9.
# -----------------------------------------------------------------------

my $r_apply_ops = http("POST /admin/apply/0001-ops-snap HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_apply_ops, qr/200/, 'ops-apply: 200 OK');

my $r_w9 = http_get('/weight');
like($r_w9, qr/weight=9/, 'ops-apply: weight set to 9 via ops-format snapshot');

# -----------------------------------------------------------------------
# 3: rollback() from the first (only) snapshot resets to base
# _pinnedId is '0001-ops-snap' after apply above.
# list=['0001-ops-snap'], idx=0, prev=undefined → _applySnapshot(null) → base.
# -----------------------------------------------------------------------

my $r_rb_base = http_get('/rb');
like($r_rb_base, qr{"rolledBackTo"\s*:\s*"base"}, 'rb-to-base: rollback from first snapshot resets to base');

# -----------------------------------------------------------------------
# 4-5: POST /admin/apply/nonexistent → 404 + JSON error
# -----------------------------------------------------------------------

my $r_bad_apply = http("POST /admin/apply/no-such-snap HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_bad_apply, qr{404},      'apply-missing: 404 for nonexistent snapshot');
like($r_bad_apply, qr{"error":}, 'apply-missing: JSON error body');

# -----------------------------------------------------------------------
# 6-7: GET /admin/snapshots/nonexistent → 404
# -----------------------------------------------------------------------

my $r_bad_get = http_get('/admin/snapshots/no-such-snap');
like($r_bad_get, qr{404},      'snap-get-missing: 404');
like($r_bad_get, qr{"error":}, 'snap-get-missing: JSON error body');

# -----------------------------------------------------------------------
# 8: Unknown admin route → 404 JSON error
# -----------------------------------------------------------------------

my $r_unknown = http_get('/admin/unknown-path');
like($r_unknown, qr{404}, 'unknown-route: 404');

# -----------------------------------------------------------------------
# 9-10: POST /admin/snapshots with invalid JSON body → 400
# -----------------------------------------------------------------------

my $bad_json = 'not json at all';
my $r_bad_body = http(<<"EOF");
POST /admin/snapshots HTTP/1.0
Host: localhost
Content-Type: application/json
Content-Length: @{[length($bad_json)]}

$bad_json
EOF
like($r_bad_body, qr{400},      'bad-json-body: 400');
like($r_bad_body, qr{"error":}, 'bad-json-body: JSON error body');

# -----------------------------------------------------------------------
# 11-12: POST /admin/snapshots without a name → 400
# -----------------------------------------------------------------------

my $no_name = '{}';
my $r_no_name = http(<<"EOF");
POST /admin/snapshots HTTP/1.0
Host: localhost
Content-Type: application/json
Content-Length: @{[length($no_name)]}

$no_name
EOF
like($r_no_name, qr{400},      'no-name: 400');
like($r_no_name, qr{"error":}, 'no-name: error field present');

# -----------------------------------------------------------------------
# 13-14: pin(null) clears pin; rollback goes to most recent snapshot
#
# Create a second snapshot (weight=13) and apply it (_pinnedId='0002-w13').
# pin(null) clears _pinnedId.
# rollback() with no pin: idx=list.length=2, prev=list[1]='0002-w13'
# → applies 0002-w13 again (resets to base then sets weight=13).
# -----------------------------------------------------------------------

# Create 0002-w13 snapshot by mutating weight and snapshotting
http_get('/weight?w=13');
my $r_create = http("POST /admin/snapshots?name=w13 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my ($snap_id) = ($r_create =~ /"id"\s*:\s*"([^"]+)"/);

# Reset weight to 1 then apply snapshot (so we can verify re-apply works)
http_get('/weight?w=1');
http("POST /admin/apply/$snap_id HTTP/1.0\r\nHost: localhost\r\n\r\n");

# Clear pin
http_get('/pin?id=null');

# Rollback with no pin → most recent snapshot (0002-w13)
my $r_rb_last = http_get('/rb');
like($r_rb_last, qr{"rolledBackTo"\s*:\s*"0002}, 'pin-null-rollback: rolls back to most recent snapshot');

my $r_wcheck = http_get('/weight');
like($r_wcheck, qr/weight=13/, 'pin-null-rollback: weight is 13 after rollback to last snapshot');

# -----------------------------------------------------------------------
# 15-16: listSnapshots returns sorted order
# We now have 0001-ops-snap and 0002-w13; list must be sorted ascending.
# -----------------------------------------------------------------------

my $r_list = http_get('/admin/snapshots');
like($r_list, qr/0001.*0002/s, 'list-sorted: 0001 appears before 0002');
like($r_list, qr/0002.*w13/s,  'list-sorted: 0002-w13 present in list');

$t->stop();
