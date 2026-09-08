#!/usr/bin/perl

# Two defects in the body-filter API, both silent.
#
# 1. addBodyFilter(fn, opts) DROPPED opts. The dispatch is
#
#        if (argc == 1 || JS_IsFunction(ctx, argv[0])) { ... opts = UNDEFINED }
#
#    so the two-argument generator form fell into the one-argument branch and
#    hard-coded JS_UNDEFINED. Naming a filter, or ordering it with
#    before/after/priority, produced no error and no effect -- while the
#    sibling addHeaderFilter(fn, opts) honoured them. Nothing caught it
#    because nothing had ever passed opts to the generator form.
#
# 2. removeBodyFilter left jlcf->body_filter_has_wb set. Adding a
#    whole-body/generator filter arms whole-body BUFFERING; removal took the
#    entry out of the list but never cleared the flag, so every response
#    through the location kept being accumulated in memory for a filter that
#    no longer existed. The flag is a summary of the list, so it is now
#    derived from the list on removal.
#
#    Honest scope note: has_wb has no JS-visible surface, so the checks below
#    pin the OBSERVABLE consequences (the list empties, filters still work
#    after add/remove cycles) and not the flag itself. #1 is directly
#    observable via getBodyFilter and is asserted properly.

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
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /opts/  { }
        location /cycle/ { }
        location /churn/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
(function () {
    var locs = nginx.http.servers[0].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    /* ---- 1. the generator form must honour opts ---------------------- */
    var L = by['/opts/'];

    L.addBodyFilter(async function* (chunks, req) {
        for await (var c of chunks) { yield c; }
    }, { name: 'gen1', priority: 7 });

    /* the explicit (mode, fn, opts) form has always worked — the control */
    L.addBodyFilter('streamingSync', function (r, chunk, flags) {
        if (chunk) { r.sendBuffer(chunk); }
    }, { name: 'str1', priority: 9 });

    var gen1 = L.getBodyFilter('gen1');
    var str1 = L.getBodyFilter('str1');
    var miss = L.getBodyFilter('nope');

    L.handler = function (r) {
        r.respond(200, {
            'X-Gen-Name': String(gen1 ? gen1.name     : 'null'),
            'X-Gen-Prio': String(gen1 ? gen1.priority : 'null'),
            'X-Str-Name': String(str1 ? str1.name     : 'null'),
            'X-Miss':     String(miss === null ? 'null' : miss),
            'X-Count':    String(L.bodyFilters.length),
        }, 'ok');
    };

    /* ---- 2. add a whole-body filter, then remove it ------------------ */
    var C = by['/cycle/'];

    C.addBodyFilter('wholeBodySync', function (r, body) {
        return String(body).toUpperCase();
    }, { name: 'wb' });

    var afterAdd = C.bodyFilters.length;

    C.removeBodyFilter('wb');

    var afterRemove = C.bodyFilters.length;
    var gone        = C.getBodyFilter('wb');

    /* Re-add a STREAMING filter after the whole-body one is gone. With the
     * flag stuck on, this location stays in whole-body buffering mode for a
     * filter that no longer exists. */
    C.addBodyFilter('streamingSync', function (r, chunk, flags) {
        if (chunk) { r.sendBuffer(String(chunk).replace(/a/g, 'A')); }
    }, { name: 'after' });

    /* ---- 3. add/remove churn must not grow the registry -------------- */
    var K = by['/churn/'];
    var reg0 = globalThis.__ngx_filters__ ? globalThis.__ngx_filters__.length : -1;

    for (var n = 0; n < 200; n++) {
        K.addHeaderFilter(function (r) {}, { name: 'churn' + n });
        K.removeHeaderFilter('churn' + n);
    }

    var reg1 = globalThis.__ngx_filters__ ? globalThis.__ngx_filters__.length : -1;

    /* One filter left registered afterwards, to prove reuse did not corrupt
     * the mapping: the surviving entry must still resolve to ITS function. */
    K.addHeaderFilter(function (r) { r.setHeader('x-churn', 'live'); },
                      { name: 'survivor' });

    K.handler = function (r) {
        var got = K.getBodyFilter('survivor');   /* wrong list on purpose */
        var hdr = K.getHeaderFilter('survivor');
        r.respond(200, {
            'X-Reg-Before': String(reg0),
            'X-Reg-After':  String(reg1),
            'X-Reg-Growth': String(reg1 - reg0),
            'X-Survivor':   String(hdr ? hdr.name : 'null'),
            'X-Wrong-List': String(got === null ? 'null' : 'found'),
        }, 'ok');
    };

    C.handler = function (r) {
        r.respond(200, {
            'X-After-Add':    String(afterAdd),
            'X-After-Remove': String(afterRemove),
            'X-Gone':         String(gone === null ? 'null' : 'present'),
            'X-Now':          String(C.bodyFilters.length),
            'X-Now-Name':     String(C.bodyFilters[0] && C.bodyFilters[0].name),
        }, 'banana');
    };
})();
JS

$t->run()->plan(15);

###############################################################################

sub hdr {
    my ($r, $name) = @_;
    return $r =~ /^\Q$name\E:\s*(.+?)\x0d?$/mi ? $1 : undef;
}

my $r = http_get('/opts/');

# 1 — the fix: opts reach the generator form
is(hdr($r, 'X-Gen-Name'), 'gen1',
   'addBodyFilter(fn, opts): name reaches the entry (was silently dropped)');
is(hdr($r, 'X-Gen-Prio'), '7',
   'addBodyFilter(fn, opts): priority reaches the entry');
is(hdr($r, 'X-Miss'), 'null',
   'getBodyFilter on an unknown name yields null');
# the (mode, fn, opts) form is the control — it always worked
is(hdr($r, 'X-Str-Name'), 'str1',
   'addBodyFilter(mode, fn, opts): unchanged');
is(hdr($r, 'X-Count'), '2', 'both filters registered');

my $r2 = http_get('/cycle/');

# 2 — removal really removes, and the location still works afterwards
is(hdr($r2, 'X-After-Add'), '1',    'whole-body filter registered');
is(hdr($r2, 'X-After-Remove'), '0', 'removeBodyFilter empties the list');
is(hdr($r2, 'X-Gone'), 'null',      'the removed filter is no longer findable');
is(hdr($r2, 'X-Now'), '1',          'a filter re-added after removal registers');
is(hdr($r2, 'X-Now-Name'), 'after', 'and it is the new one');
like($r2, qr/bAnAnA/,
     'the streaming filter added after the whole-body one still transforms');

# 3 — the filter registry must not grow without bound.
#
# __ngx_filters__ is a process-lifetime global that roots every registered
# filter function. Registration used to append at length while removal only
# nulled the slot, so 200 add/remove cycles grew it by 200 and it never shrank
# -- an unbounded growth an untrusted tenant can drive in a loop.
my $r3 = http_get('/churn/');
like($r3, qr/200 OK/,               'churn: 200');
my ($growth) = $r3 =~ /X-Reg-Growth:\s*(-?\d+)/i;
diag("registry growth over 200 add/remove cycles: " . (defined $growth ? $growth : 'n/a'));
cmp_ok($growth, '<=', 4,
       "filter registry does not grow with add/remove churn (grew $growth over 200)");
like($r3, qr/X-Survivor:\s*survivor/i,
     'a filter registered after the churn still resolves to its own entry');
like($r3, qr/X-Wrong-List:\s*null/i,
     'and it is not visible on the other filter list');
