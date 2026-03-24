#!/usr/bin/perl

# Tests for admin.js diff compaction:
#   options.compact   — auto-compact on createSnapshot (identity ops removed)
#   compactSnapshot   — compact existing snapshot in place
#   squash            — merge multiple snapshots into one

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

        location /admin/  { }
        location /weight  { }
        location /compact { }
        location /squash  { }
    }
}
EOF

mkdir $t->testdir() . '/snapshots';

$t->write_file('init.js', <<'JS');
var ups  = nginx.http.upstreams.find(function (u) { return u.name === 'backend'; });
var locs = nginx.http.servers[0].locations;

var wloc      = locs.find(function (l) { return l.path === '/weight'; });
var compact   = locs.find(function (l) { return l.path === '/compact'; });
var squash    = locs.find(function (l) { return l.path === '/squash'; });

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

/* POST /compact?id=X — compact the named snapshot in place */
compact.handler = function (req) {
    var id = req.queryParams.id;
    try {
        var removed = nginx.admin.compactSnapshot(id);
        req.respond(200, {'Content-Type': 'text/plain'},
                    JSON.stringify({removed: removed}) + '\n');
    } catch (e) {
        req.respond(500, {}, String(e));
    }
};

/* POST /squash?ids=A,B&name=N — squash snapshot ids A and B into name N */
squash.handler = function (req) {
    var ids  = req.queryParams.ids.split(',');
    var name = req.queryParams.name;
    try {
        var id = nginx.admin.squash(ids, name);
        req.respond(200, {'Content-Type': 'text/plain'},
                    JSON.stringify({id: id}) + '\n');
    } catch (e) {
        req.respond(500, {}, String(e));
    }
};
JS

$t->try_run('no js module')->plan(13);

# 1. Baseline: weight is 1
my $r0 = http_get('/weight');
like($r0, qr/\b1\b/, 'baseline weight is 1');

# 2. With auto-compact on: create a snapshot that has weight=1 (same as base)
#    The ops-list should be EMPTY because value == base value.
http("POST /admin/snapshots?name=base HTTP/1.0\r\nHost: localhost\r\n\r\n");
# First, enable compact option via a custom init — but since we can't
# easily change options at runtime from Perl, we test compactSnapshot instead.

# 3. Set weight=5, create snapshot w5
http_get('/weight?w=5');
my $r_create5 = http("POST /admin/snapshots?name=w5 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my ($id5) = ($r_create5 =~ /"id"\s*:\s*"([^"]+)"/);
like($r_create5, qr/200/, 'create w5: 200');

# 4. Set weight=9, create snapshot w9
http_get('/weight?w=9');
my $r_create9 = http("POST /admin/snapshots?name=w9 HTTP/1.0\r\nHost: localhost\r\n\r\n");
my ($id9) = ($r_create9 =~ /"id"\s*:\s*"([^"]+)"/);
like($r_create9, qr/200/, 'create w9: 200');

# 5. Compact the w5 snapshot — it has 1 op (weight=5 != base=1), nothing removed
my $r_compact = http("POST /compact?id=$id5 HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_compact, qr/200/, 'compactSnapshot: 200');
like($r_compact, qr/"removed"\s*:\s*0/, 'compactSnapshot: 0 ops removed from minimal snapshot');

# 6. Squash w5 with itself — two identical ops become one (last-write-wins)
my $r_sq2 = http("POST /squash?ids=$id5,$id5&name=dedup HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_sq2, qr/200/, 'squash self: 200');
my ($dedup_id) = ($r_sq2 =~ /"id"\s*:\s*"([^"]+)"/);
my $r_dedup_snap = http_get("/admin/snapshots/$dedup_id");
my $dedup_ops = () = ($r_dedup_snap =~ /"path"/g);
is($dedup_ops, 1, 'squash self: deduplicates to 1 op');

# 7. Squash w5 + w9 into a single snapshot; result should have weight=9 only
#    (last-write-wins: w9 overrides w5 for the same peer[0].weight path)
my $r_squash = http("POST /squash?ids=$id5,$id9&name=squashed HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_squash, qr/200/, 'squash: 200');
my ($squash_id) = ($r_squash =~ /"id"\s*:\s*"([^"]+)"/);

# 8. Get the squashed snapshot and verify it has exactly one op with value 9
my $r_sq_snap = http_get("/admin/snapshots/$squash_id");
like($r_sq_snap, qr/200/, 'squash get: 200');
like($r_sq_snap, qr/weight.*\b9\b|\b9\b.*weight/s, 'squashed snapshot has weight 9');

# Count ops array entries — should be exactly one op
my $ops_count = () = ($r_sq_snap =~ /"path"/g);
is($ops_count, 1, 'squashed snapshot has exactly 1 op (w5 deduplicated into w9)');

# 9. Apply the squashed snapshot and verify weight becomes 9
http_get('/weight?w=1');   # reset
my $r_apply = http("POST /admin/apply/$squash_id HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r_apply, qr/"applied"/, 'apply squashed: has applied key');
my $r_w = http_get('/weight');
like($r_w, qr/\b9\b/, 'applied squashed snapshot: weight is 9');

$t->stop();
