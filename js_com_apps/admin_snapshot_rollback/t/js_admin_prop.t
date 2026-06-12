#!/usr/bin/perl

# Tests for {prop} named-descriptor ops in admin.js.

use warnings;
use strict;
use Test::More;
use File::Spec;
use Cwd qw(abs_path);
use URI::Escape qw(uri_escape);

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
js_source %%TESTDIR%%/prop_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream prop_backend {
        server 127.0.0.1:%%PORT_8091%% weight=5;
        server 127.0.0.2:%%PORT_8091%% weight=3;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /admin/   { }
        location /weight/  { }
        location /state/   { }
        location /apply/   { }
        location /raw/     { }
        location /compact/ { }
        location /squash/  { }
    }
}
EOF

mkdir $t->testdir() . '/snapshots';

# prop_init.js reads peer addresses dynamically — no port literals needed.
$t->write_file('prop_init.js', <<'JS');
var _u = nginx.http.upstreams.find(function(u){ return u.name==='prop_backend'; });
var _addr0 = _u.peers[0].address;
var _addr1 = _u.peers[1].address;

// Declare managed props using live peer addresses (resolved at init-conf time)
nginx.admin.init({
    props: [
        { upstream: 'prop_backend', peer: _addr0, property: 'weight', default: 5 },
        { upstream: 'prop_backend', peer: _addr1, property: 'weight', default: 3 }
    ]
});

var locs = nginx.http.servers[0].locations;
function loc(p){ return locs.find(function(l){ return l.path===p; }); }

// /weight/ — current peer weights
loc('/weight/').handler = function(req) {
    req.respond(200, {'Content-Type':'application/json'},
        JSON.stringify({p0:_u.peers[0].weight, p1:_u.peers[1].weight})+'\n');
};
// /state/ — admin.state()
loc('/state/').handler = function(req) {
    req.respond(200, {'Content-Type':'application/json'},
        JSON.stringify(nginx.admin.state())+'\n');
};
// /addrs/ — peer addresses (for building prop descriptors in tests)
loc('/raw/').handler = function(req) {
    /* GET /raw/?info=1 — return peer addresses for test harness */
    if (req.queryParams.info) {
        req.respond(200, {'Content-Type':'application/json'},
            JSON.stringify({addr0:_addr0, addr1:_addr1})+'\n');
        return;
    }
    try {
        var id = nginx.admin.createRawSnapshot(
            req.queryParams.name, JSON.parse(req.queryParams.ops));
        req.respond(200, {'Content-Type':'application/json'},
            JSON.stringify({id:id})+'\n');
    } catch(e){ req.respond(500, {}, String(e)); }
};
loc('/apply/').handler = function(req) {
    try {
        nginx.admin.applySnapshot(req.queryParams.id);
        req.respond(200, {}, 'ok');
    } catch(e){ req.respond(500, {}, String(e)); }
};
loc('/compact/').handler = function(req) {
    try {
        var r = nginx.admin.compactSnapshot(req.queryParams.id);
        req.respond(200, {'Content-Type':'application/json'},
            JSON.stringify({id:req.queryParams.id, removed:r})+'\n');
    } catch(e){ req.respond(500, {}, String(e)); }
};
loc('/squash/').handler = function(req) {
    try {
        var id = nginx.admin.squash(req.queryParams.ids.split(','), req.queryParams.name);
        req.respond(200, {'Content-Type':'application/json'},
            JSON.stringify({id:id})+'\n');
    } catch(e){ req.respond(500, {}, String(e)); }
};
JS

$t->try_run('no js module')->plan(23);

sub j { my $r = shift; $r =~ s/.*?\r\n\r\n//s; $r }

# Fetch live peer addresses from the running nginx
my $info = j http_get('/raw/?info=1');
my ($addr0) = $info =~ /"addr0":"([^"]+)"/;
my ($addr1) = $info =~ /"addr1":"([^"]+)"/;

my $p0 = '{"upstream":"prop_backend","peer":"' . $addr0 . '","property":"weight"}';
my $p1 = '{"upstream":"prop_backend","peer":"' . $addr1 . '","property":"weight"}';

# ── 1. Initial peer weights ───────────────────────────────────────────────────
my $w = j http_get('/weight/');
like($w, qr/"p0":5/, 'initial peer 0 weight=5');
like($w, qr/"p1":3/, 'initial peer 1 weight=3');

# ── 2. {prop} raw snapshot — apply by stable peer address ────────────────────
my $ops = '[{"prop":' . $p0 . ',"value":2},{"prop":' . $p1 . ',"value":8}]';
my $r2  = j http_get('/raw/?name=rebalance&ops=' . uri_escape($ops));
like($r2, qr/"id"/, 'createRawSnapshot with {prop} ops succeeds');
my ($id1) = $r2 =~ /"id":"([^"]+)"/;

j http_get("/apply/?id=$id1");
sleep 0.2;

$w = j http_get('/weight/');
like($w, qr/"p0":2/, 'after apply: peer 0 weight=2');
like($w, qr/"p1":8/, 'after apply: peer 1 weight=8');

# ── 3. admin.state() includes managed props ───────────────────────────────────
my $st = j http_get('/state/');
like($st, qr/"props"/, 'state() has props sub-object');
like($st, qr/prop_backend.*?weight.*?[28]/s, 'state() shows live peer weight');

# ── 4. compactOps — two {prop} ops for same descriptor → last wins ────────────
my $noisy = '[{"prop":' . $p0 . ',"value":9},{"prop":' . $p0 . ',"value":4}]';
my $rn   = j http_get('/raw/?name=noisy&ops=' . uri_escape($noisy));
my ($id_noisy) = $rn =~ /"id":"([^"]+)"/;

my $cr = j http_get("/compact/?id=$id_noisy");
like($cr, qr/"removed":1/, 'compactOps removes 1 duplicate {prop} op');

my $snap_c = j http_get("/admin/snapshots/$id_noisy");
like($snap_c,   qr/"value": *4/, 'compact: last value (4) kept');
unlike($snap_c, qr/"value": *9/, 'compact: earlier value (9) removed');

# ── 5. createSnapshot auto-captures declared managed props ────────────────────
my $auto = j http("POST /admin/snapshots?name=auto-prop HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($auto, qr/"id"/, 'auto-snapshot created');
my ($id_auto) = $auto =~ /"id":"([^"]+)"/;

my $sc = j http_get("/admin/snapshots/$id_auto");
like($sc, qr/"prop"/, 'auto-snapshot contains {prop} op');
like($sc, qr/"value": *2/, 'auto-snapshot captures peer 0 weight=2');
like($sc, qr/"value": *8/, 'auto-snapshot captures peer 1 weight=8');

# ── 6. Rollback restores defaults ────────────────────────────────────────────
http("POST /admin/rollback HTTP/1.0\r\nHost: localhost\r\n\r\n") for 1..3;
sleep 0.2;

$w = j http_get('/weight/');
like($w, qr/"p0":5/, 'after rollback-to-base: peer 0 weight=5');
like($w, qr/"p1":3/, 'after rollback-to-base: peer 1 weight=3');

# ── 7. Restore from auto-snapshot ────────────────────────────────────────────
j http_get("/apply/?id=$id_auto");
sleep 0.2;

$w = j http_get('/weight/');
like($w, qr/"p0":2/, 'restore from auto-snapshot: peer 0 weight=2');
like($w, qr/"p1":8/, 'restore from auto-snapshot: peer 1 weight=8');

# ── 8. squash — last-write-wins for {prop} ops ────────────────────────────────
my $sq1 = j http_get('/raw/?name=sq1&ops=' . uri_escape('[{"prop":' . $p0 . ',"value":1}]'));
my ($sq1_id) = $sq1 =~ /"id":"([^"]+)"/;
my $sq2 = j http_get('/raw/?name=sq2&ops=' . uri_escape('[{"prop":' . $p0 . ',"value":6}]'));
my ($sq2_id) = $sq2 =~ /"id":"([^"]+)"/;

my $sq = j http_get("/squash/?ids=$sq1_id,$sq2_id&name=merged");
like($sq, qr/"id"/, 'squash returns id');
my ($merged_id) = $sq =~ /"id":"([^"]+)"/;

my $merged = j http_get("/admin/snapshots/$merged_id");
like($merged,   qr/"value": *6/, 'squash: last value wins (=6)');
unlike($merged, qr/"value": *1/, 'squash: earlier value (1) removed');

# ── 9. REST endpoints ─────────────────────────────────────────────────────────
like(j(http_get('/admin/worker')), qr/"worker"/, 'GET /admin/worker works');
my $cr2 = j http("POST /admin/compact/$id_noisy HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($cr2, qr/"removed"/, 'POST /admin/compact/:id via REST works');
