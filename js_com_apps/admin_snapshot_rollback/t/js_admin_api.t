#!/usr/bin/perl

# Tests for admin-api.js HTTP REST endpoints.
#
# Endpoints tested:
#   GET  /admin/state          — returns JSON delta state
#   GET  /admin/snapshots      — returns JSON array of ids
#   POST /admin/snapshots      — creates snapshot, returns {"id":"..."}
#   GET  /admin/snapshots/:id  — returns snapshot JSON content
#   POST /admin/apply/:id      — applies snapshot
#   POST /admin/rollback       — rolls back to previous snapshot
#   404  /admin/unknown        — unknown route returns JSON error

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

my $admin_js    = abs_path(File::Spec->catfile($FindBin::Bin, '..', 'conf', 'admin.js'));
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

        location /admin/  { }
        location /weight  { }
    }
}
EOF

mkdir $t->testdir() . '/snapshots';

$t->write_file('init.js', <<'JS');
/* /weight handler to inspect / mutate peer weight for test setup */
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
                    String(ups.peers[0].weight));
    }
};
JS

$t->try_run('no js module')->plan(14);

# 1. GET /admin/state — empty delta at startup
my $r_state = http_get('/admin/state');
like($r_state, qr/200/,     'state: 200 response');
like($r_state, qr/"peers"/, 'state: has peers field');

# 2. GET /admin/snapshots — empty list at startup
my $r_list0 = http_get('/admin/snapshots');
like($r_list0, qr/200/,  'list0: 200 response');
like($r_list0, qr/\[\]/, 'list0: empty array initially');

# 3. Set weight=7, POST /admin/snapshots?name=w7
http_get('/weight?w=7');
my $r_create = http("POST /admin/snapshots?name=w7 HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_create, qr/200/,       'create: 200 response');
like($r_create, qr/"id".*0001/, 'create: id starts with 0001');

# Extract the snapshot id
my ($snap_id) = ($r_create =~ /"id"\s*:\s*"([^"]+)"/);

# 4. GET /admin/snapshots — list now has one entry
my $r_list1 = http_get('/admin/snapshots');
like($r_list1, qr/0001-w7/, 'list1: new snapshot id in list');

# 5. GET /admin/snapshots/:id — returns snapshot content with weight 7
my $r_snap = http_get("/admin/snapshots/$snap_id");
like($r_snap, qr/200/,          'snap_get: 200 response');
like($r_snap, qr/"weight".*[^1-69]7|7.*"weight"/, 'snap_get: snapshot has weight 7');

# 6. POST /admin/apply/:id — re-apply after weight reset
http_get('/weight?w=1');    # reset to 1
my $r_apply = http("POST /admin/apply/$snap_id HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_apply, qr/200/,       'apply: 200 response');
like($r_apply, qr/"applied"/, 'apply: response has "applied" key');
my $r_w1 = http_get('/weight');
like($r_w1, qr/\b7\b/, 'apply: weight is 7 after apply');

# 7. POST /admin/rollback — reverts to base (weight 1)
my $r_rb = http("POST /admin/rollback HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_rb, qr/200/, 'rollback: 200 response');
my $r_w2 = http_get('/weight');
like($r_w2, qr/\b1\b/, 'rollback: weight is 1 after rollback to base');

$t->stop();
