#!/usr/bin/perl
#
# Tests for snapshot() / restore() on NginxLocation, NginxPeer, and
# NginxUpstream COM objects.
#

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

###############################################################################

select STDOUT;
$| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/snap.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream backend {
        server 127.0.0.1:8181;
        server 127.0.0.1:8182 backup;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /basic_restore    { }
        location /multi_restore    { }
        location /proxy_restore    { proxy_pass http://backend; }
        location /mode_global      { }
        location /two_snaps        { }
        location /idempotent       { }
        location /gzip_restore     { gzip on; gzip_comp_level 6; }
        location /peer_restore     { }
        location /upstream_restore { }
    }
}
EOF

$t->write_file('snap.js', <<'EOF'

/* ================================================================== */
/* Config-phase: install request handlers                               */
/* ================================================================== */

(function installHandlers() {
    const locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    set('/basic_restore',    snap_basic_restore);
    set('/multi_restore',    snap_multi_restore);
    set('/proxy_restore',    snap_proxy_restore);
    set('/mode_global',      snap_mode_global);
    set('/two_snaps',        snap_two_snaps);
    set('/idempotent',       snap_idempotent);
    set('/gzip_restore',     snap_gzip_restore);
    set('/peer_restore',     snap_peer_restore);
    set('/upstream_restore', snap_upstream_restore);
})();

/* ================================================================== */
/* Basic snapshot / restore of location.root                           */
/* ================================================================== */

function snap_basic_restore(req) {
    const orig = req.location.root;
    const snap = req.location.snapshot();

    req.location.root = '/snap-changed';

    if (req.location.root !== '/snap-changed') {
        req.respond(200, {}, 'FAIL: mutation did not apply: ' + req.location.root);
        return;
    }

    snap.restore();

    if (req.location.root !== orig) {
        req.respond(200, {}, 'FAIL: restore wrong: got=' + req.location.root
                    + ' want=' + orig);
        return;
    }

    snap.restore();   /* idempotent cleanup */
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* Multiple mutations then single restore                               */
/* ================================================================== */

function snap_multi_restore(req) {
    const orig = req.location.root;
    const snap = req.location.snapshot();

    req.location.root = '/first';
    req.location.root = '/second';
    req.location.root = '/third';

    if (req.location.root !== '/third') {
        req.respond(200, {}, 'FAIL: expected /third got ' + req.location.root);
        return;
    }

    snap.restore();

    if (req.location.root !== orig) {
        req.respond(200, {}, 'FAIL: restore got=' + req.location.root
                    + ' want=' + orig);
        return;
    }

    snap.restore();
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* Proxy sub-object captured and restored                               */
/* ================================================================== */

function snap_proxy_restore(req) {
    const proxy = req.location.proxy;
    if (!proxy) {
        req.respond(200, {}, 'SKIP: no proxy');
        return;
    }

    const origTimeout = proxy.readTimeout;
    const snap = req.location.snapshot();

    req.location.proxy.readTimeout = 9999;

    if (req.location.proxy.readTimeout !== 9999) {
        req.respond(200, {}, 'FAIL: proxy mutation did not apply');
        return;
    }

    snap.restore();

    if (req.location.proxy.readTimeout !== origTimeout) {
        req.respond(200, {}, 'FAIL: proxy restore got='
                    + req.location.proxy.readTimeout
                    + ' want=' + origTimeout);
        return;
    }

    snap.restore();
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* restore({ mode: 'global' }) overrides write_mode for the call       */
/* ================================================================== */

function snap_mode_global(req) {
    req.location.setWriteMode('local');
    req.location.setReadMode('local');

    const orig = req.location.root;
    const snap = req.location.snapshot();   /* captures local (= global, no mut yet) */

    req.location.root = '/local-only';      /* write_mode=local → local copy only */

    if (req.location.root !== '/local-only') {
        req.respond(200, {}, 'FAIL: local mutation did not apply');
        return;
    }

    /* Force restore into global */
    snap.restore({ mode: 'global' });

    /* Read from global to verify the restore wrote there */
    req.location.setReadMode('global');
    const globalVal = req.location.root;

    if (globalVal !== orig) {
        req.respond(200, {}, 'FAIL: global not restored: got=' + globalVal
                    + ' want=' + orig);
        return;
    }

    snap.restore({ mode: 'global' });  /* cleanup */
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* Two independent snapshots do not interfere                           */
/* ================================================================== */

function snap_two_snaps(req) {
    const orig  = req.location.root;
    const snap1 = req.location.snapshot();

    req.location.root = '/state-a';
    const snap2 = req.location.snapshot();   /* captures /state-a */

    req.location.root = '/state-b';

    snap2.restore();
    if (req.location.root !== '/state-a') {
        req.respond(200, {}, 'FAIL: snap2 restore got=' + req.location.root
                    + ' want=/state-a');
        return;
    }

    snap1.restore();
    if (req.location.root !== orig) {
        req.respond(200, {}, 'FAIL: snap1 restore got=' + req.location.root
                    + ' want=' + orig);
        return;
    }

    snap1.restore();
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* restore() is idempotent — calling it multiple times is safe         */
/* ================================================================== */

function snap_idempotent(req) {
    const orig = req.location.root;
    const snap = req.location.snapshot();

    req.location.root = '/mutated';
    snap.restore();
    snap.restore();
    snap.restore();

    if (req.location.root !== orig) {
        req.respond(200, {}, 'FAIL: after 3 restores got=' + req.location.root);
        return;
    }

    snap.restore();
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* Gzip sub-object snapshot                                             */
/* ================================================================== */

function snap_gzip_restore(req) {
    const gz = req.location.gzip;
    if (!gz) {
        req.respond(200, {}, 'SKIP: no gzip');
        return;
    }

    const origLevel = gz.level;
    const snap = req.location.snapshot();

    req.location.gzip.level = 1;

    if (req.location.gzip.level !== 1) {
        req.respond(200, {}, 'FAIL: gzip mutation did not apply');
        return;
    }

    snap.restore();

    if (req.location.gzip.level !== origLevel) {
        req.respond(200, {}, 'FAIL: gzip restore got='
                    + req.location.gzip.level + ' want=' + origLevel);
        return;
    }

    snap.restore();
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* peer.snapshot() / restore() — config-phase NginxPeer                */
/* ================================================================== */

function snap_peer_restore(req) {
    const ups = nginx.http.upstreams[0];
    if (!ups) {
        req.respond(200, {}, 'SKIP: no upstream');
        return;
    }

    const peer       = ups.peers[0];
    const origWeight = peer.weight;
    const snap       = peer.snapshot();

    peer.weight = 999;

    if (peer.weight !== 999) {
        req.respond(200, {}, 'FAIL: peer mutation did not apply');
        return;
    }

    snap.restore();

    if (peer.weight !== origWeight) {
        req.respond(200, {}, 'FAIL: peer restore got=' + peer.weight
                    + ' want=' + origWeight);
        return;
    }

    snap.restore();
    req.respond(200, {}, 'OK');
}

/* ================================================================== */
/* upstream.snapshot() / restore() — captures all peers                */
/* ================================================================== */

function snap_upstream_restore(req) {
    const ups = nginx.http.upstreams[0];
    if (!ups) {
        req.respond(200, {}, 'SKIP: no upstream');
        return;
    }

    const origWeights = ups.peers.map(p => p.weight).join(',');
    const snap        = ups.snapshot();

    ups.peers.forEach(p => { p.weight = 42; });

    const allFortyTwo = ups.peers.every(p => p.weight === 42);
    if (!allFortyTwo) {
        req.respond(200, {}, 'FAIL: not all weights set to 42');
        return;
    }

    snap.restore();

    const restoredWeights = ups.peers.map(p => p.weight).join(',');
    if (restoredWeights !== origWeights) {
        req.respond(200, {}, 'FAIL: upstream restore got=' + restoredWeights
                    + ' want=' + origWeights);
        return;
    }

    snap.restore();
    req.respond(200, {}, 'OK');
}

EOF
);

$t->try_run('no js module')->plan(9);

###############################################################################

like(http_get('/basic_restore'),    qr/OK/, 'basic_restore: snapshot and restore work');
like(http_get('/multi_restore'),    qr/OK/, 'multi_restore: multiple mutations then restore');
like(http_get('/proxy_restore'),    qr/OK|SKIP/, 'proxy_restore: sub-object snapshot works');
like(http_get('/mode_global'),      qr/OK/, 'mode_global: restore({mode:global}) overrides write_mode');
like(http_get('/two_snaps'),        qr/OK/, 'two_snaps: two independent snapshots');
like(http_get('/idempotent'),       qr/OK/, 'idempotent: multiple restore calls are safe');
like(http_get('/gzip_restore'),     qr/OK|SKIP/, 'gzip_restore: gzip sub-object snapshot');
like(http_get('/peer_restore'),     qr/OK|SKIP/, 'peer_restore: NginxPeer snapshot works');
like(http_get('/upstream_restore'), qr/OK|SKIP/, 'upstream_restore: NginxUpstream snapshot works');
