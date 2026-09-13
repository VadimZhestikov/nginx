#!/usr/bin/perl

# nginx.shared — the store is an OPEN-ADDRESSED HASH TABLE (HOST-PERF).
#
# It used to be a linear scan over every slot, comparing 128-byte keys under the
# spinlock: 0.079 us on a near-front hit, 0.417 us on a MISS -- and a miss is
# what a rate limiter does for every new tenant key (t/tools/host-call-cost.t).
# Probing makes that ~one slot, but it buys a failure mode a scan cannot have:
# a key that is PRESENT and cannot be FOUND, because a deletion broke the probe
# chain that reaches it. Nothing in the old tests could see that -- they use a
# handful of keys, which rarely collide.
#
# So the oracle here is a MODEL. The handler keeps a plain JS object alongside
# the store, applies the same operations to both, and compares them key by key.
# A lost key, a resurrected key, a stale value, a miscounted store -- all show
# up as a disagreement rather than as a plausible-looking answer.
#
# The specific torture is deleting from the MIDDLE of a collision cluster:
# backward-shift deletion has to move the rest of the cluster back without
# moving any entry past its own home slot. Keys are chosen as k0..kN, which
# hash all over the table and fill it to ~85%, so clusters are long.

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

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /churn { }
        location /full { }
        location /expiry { }
        location /expiry-check { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var S = nginx.shared;

function clear() {
    var ks = S.keys(), i;
    for (i = 0; i < ks.length; i++) { S.delete(ks[i]); }
}

/* compare the store against the model, both ways */
function diff(model) {
    var bad = [], k, v, ks, i;

    for (k in model) {
        if (Object.prototype.hasOwnProperty.call(model, k)) {
            v = S.get(k);
            if (v !== model[k]) {
                bad.push({ key: k, want: model[k],
                           got: (v === undefined) ? 'ABSENT' : v });
            }
        }
    }

    ks = S.keys();
    for (i = 0; i < ks.length; i++) {
        if (!Object.prototype.hasOwnProperty.call(model, ks[i])) {
            bad.push({ key: ks[i], want: 'ABSENT', got: 'present' });
        }
    }

    return { bad: bad, storeKeys: ks.length, modelKeys: Object.keys(model).length };
}

/* A deterministic pseudo-random walk: insert, overwrite and delete against a
   store kept near capacity, so clusters are long and deletions land inside
   them. Seeded so a failure is reproducible. */
locs.find(function (l) { return l.path === "/churn"; }).handler = function (req) {
    clear();

    var model = {}, seed = 123456789, i, k, op, n = 0;

    function rnd(m) {          /* xorshift; no Date, no Math.random */
        seed ^= seed << 13; seed >>>= 0;
        seed ^= seed >> 17;
        seed ^= seed << 5;  seed >>>= 0;
        return seed % m;
    }

    for (i = 0; i < 4000; i++) {
        k = 'k' + rnd(210);            /* < capacity, so inserts keep landing */
        op = rnd(3);

        if (op === 2) {
            var had = Object.prototype.hasOwnProperty.call(model, k);
            var got = S.delete(k);
            if (got !== had) {
                req.respond(200, {'content-type':'application/json'},
                    JSON.stringify({ deleteDisagreed: { key: k, model: had,
                                                        store: got, at: i } }));
                return;
            }
            delete model[k];

        } else {
            var v = 'v' + i;
            S.set(k, v);
            model[k] = v;
            n++;
        }
    }

    var d = diff(model);
    d.inserts = n;
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(d));
};

/* capacity: fill until the store refuses, then check that a delete makes room
   again -- the property the old first-free-slot scan gave for free. */
locs.find(function (l) { return l.path === "/full"; }).handler = function (req) {
    clear();

    var i, filled = 0, err = null, reuse = null;

    for (i = 0; i < 400; i++) {
        try { S.set('f' + i, 'x'); filled++; }
        catch (e) { err = e.message; break; }
    }

    if (err !== null) {
        S.delete('f0');
        try { S.set('after-delete', 'y'); reuse = S.get('after-delete'); }
        catch (e2) { reuse = 'REFUSED: ' + e2.message; }
    }

    /* every key that was accepted must still be findable at full occupancy */
    var lost = 0;
    for (i = 1; i < filled; i++) {
        if (S.get('f' + i) !== 'x') { lost++; }
    }

    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ filled: filled, err: err, reuse: reuse, lost: lost,
                         keys: S.keys().length }));
};

/* expiry is reclaimed when the key is probed -- the old full scan reclaimed
   everything it walked past, so this is the property most at risk.

   IN TWO REQUESTS, and that is not incidental: ngx_time() is nginx's CACHED
   time, refreshed by the event loop. A handler that busy-waits blocks the very
   loop that would advance it, so inside one request no key can ever be seen to
   expire -- the first version of this test spun for 1100 ms and observed the
   ttl stay at 1. The sleep has to happen where nginx can run. */
locs.find(function (l) { return l.path === "/expiry"; }).handler = function (req) {
    clear();

    S.set('perm', 'p');
    S.set('gone', 'g', 1);              /* 1-second ttl */
    /* a neighbour that must survive its expiring cluster-mate */
    S.set('stay', 's');

    req.respond(200, {'content-type':'application/json'},
        JSON.stringify({ ttlPerm: S.ttl('perm'), ttlGone: S.ttl('gone'),
                         getGone: S.get('gone'), now: S.keys().length }));
};

locs.find(function (l) { return l.path === "/expiry-check"; }).handler = function (req) {
    var out = {};

    out.afterGet = (S.get('gone') === undefined) ? 'absent' : 'present';
    out.afterTtl = S.ttl('gone');
    out.afterDelete = S.delete('gone');   /* already absent -> false */
    out.permSurvives = S.get('perm');
    out.staySurvives = S.get('stay');
    out.keys = S.keys().sort();

    req.respond(200, {'content-type':'application/json'}, JSON.stringify(out));
};
JS

$t->try_run('no js module')->plan(14);

###############################################################################

my $churn = http_get('/churn');
diag($1) if $churn =~ /(\{.*\})/;

like($churn, qr/"bad":\[\]/,
     'CHURN: after 4000 mixed set/delete operations the store and the model '
     . 'agree on every key -- no key was lost by a deletion shifting the '
     . 'cluster that reaches it, and none was resurrected');
like($churn, qr/"storeKeys":(\d+),"modelKeys":\1\b/,
     'CHURN: keys() reports exactly as many keys as the model holds');
unlike($churn, qr/deleteDisagreed/,
     'CHURN: delete() reported present/absent the same way the model did, on '
     . 'every one of the deletions');

my $full = http_get('/full');
diag($1) if $full =~ /(\{.*\})/;

like($full, qr/"filled":(25[0-9]|2[0-4][0-9])/,
     'FULL: the store accepts keys up to its capacity');
like($full, qr/"err":"[^"]*store full/,
     'FULL: it refuses the key past capacity rather than overwriting one');
like($full, qr/"reuse":"y"/,
     'FULL: deleting one key makes room again -- the freed slot is reusable, '
     . 'which is the property the old first-free-slot scan provided for free '
     . 'and a tombstoning table would lose');
like($full, qr/"lost":0/,
     'FULL: at full occupancy every accepted key is still findable -- the '
     . 'longest possible probe chains still terminate on their own key');

my $set = http_get('/expiry');
diag($1) if $set =~ /(\{.*\})/;

like($set, qr/"ttlPerm":-1/, 'EXPIRY: a key with no ttl reports -1 (permanent)');
like($set, qr/"ttlGone":1/,  'EXPIRY: a key with a ttl reports seconds left');

select(undef, undef, undef, 1.4);   # in PERL, so nginx's cached time advances

my $exp = http_get('/expiry-check');
diag($1) if $exp =~ /(\{.*\})/;

like($exp, qr/"afterGet":"absent"/,
     'EXPIRY: an expired key is absent from get() -- reclaimed when probed, '
     . 'which is where the old full scan used to reclaim it incidentally');
like($exp, qr/"afterTtl":null/, 'EXPIRY: ttl() of an expired key is null');
like($exp, qr/"afterDelete":false/,
     'EXPIRY: deleting an already-expired key reports false -- it was absent, '
     . 'and delete() must not report a removal it did not make');
like($exp, qr/"permSurvives":"p"/,
     'EXPIRY: reclaiming the expired entry did not disturb the permanent key');
like($exp, qr/"staySurvives":"s"/,
     'EXPIRY: nor the neighbour that may share its cluster -- a backward shift '
     . 'during reclamation must not strand the entries behind it');
