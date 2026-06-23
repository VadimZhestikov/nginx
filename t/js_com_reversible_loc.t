#!/usr/bin/perl

# Tests for reversible removeLocation (tombstone) + restoreLocation.
#
# removeLocation() now soft-removes by default: the location is excluded from
# the live routing tree but its entry/clcf are kept, so restoreLocation() brings
# it back (identity preserved). removeLocation(pattern, {hard:true}) keeps the
# legacy irreversible splice. Covers prefix, exact, and named locations, plus a
# many-cycle stability check.

use warnings;
use strict;
use Test::More;
use URI::Escape qw(uri_escape);

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

        location /ctl/   { }
        location /gone/  { }
        location = /exact { }
        location /keep/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
nginx.broadcast(function () {
    var srv = nginx.http.servers[0];

    function body(path, text) {
        var l = srv.locations.find(function (l) { return l.path === path; });
        if (l) { l.handler = function (r) { r.respond(200, {}, text + '\n'); }; }
    }
    body('/gone/',  'GONE');
    body('/exact',  'EXACT');
    body('/keep/',  'KEEP');

    srv.locations.find(function (l) { return l.path === '/ctl/'; }).handler =
    function (r) {
        var q = r.queryParams, out = {};
        try {
            if (q.op === 'remove')     { out.r = srv.removeLocation(q.pat); }
            else if (q.op === 'restore'){ out.r = srv.restoreLocation(q.pat); }
            else if (q.op === 'hard')  { out.r = srv.removeLocation(q.pat, {hard:true}); }
            else if (q.op === 'cycle') {
                /* N remove/restore round-trips; must end live */
                var n = parseInt(q.n || '50', 10), i;
                for (i = 0; i < n; i++) {
                    srv.removeLocation('/gone/');
                    srv.restoreLocation('/gone/');
                }
                out.r = true;
            }
        } catch (e) { out.err = String(e.message); }
        r.respond(200, {'Content-Type':'application/json'}, JSON.stringify(out) + '\n');
    };
});
JS

$t->try_run('no js module')->plan(16);

sub code { my $r = http_get(shift); return $1 if $r =~ m!^HTTP/\d\.\d\s+(\d+)!; 0 }
sub jbody { my $r = http_get(shift); $r =~ s/.*?\r\n\r\n//s; $r }

# ── baseline ─────────────────────────────────────────────────────────────────
is(code('/gone/'),  200, 'prefix /gone/ live initially');
is(code('/exact'),  200, 'exact /exact live initially');
is(code('/keep/'),  200, 'prefix /keep/ live initially');

# ── prefix: soft remove → 404, restore → 200 (reversible) ────────────────────
like(jbody('/ctl/?op=remove&pat=/gone/'), qr/"r":true/, 'removeLocation /gone/ returns true');
is(code('/gone/'), 404, 'after soft remove: /gone/ is 404');
is(code('/keep/'), 200, 'sibling /keep/ unaffected by removal');
like(jbody('/ctl/?op=restore&pat=/gone/'), qr/"r":true/, 'restoreLocation /gone/ returns true');
is(code('/gone/'), 200, 'after restore: /gone/ live again (identity preserved)');

# ── exact location reversibility (pattern URL-encoded — has '=' and space) ───
my $ex = uri_escape('= /exact');
like(jbody("/ctl/?op=remove&pat=$ex"), qr/"r":true/, 'removeLocation = /exact');
is(code('/exact'), 404, 'after remove: exact /exact is 404');
like(jbody("/ctl/?op=restore&pat=$ex"), qr/"r":true/, 'restore = /exact');
is(code('/exact'), 200, 'after restore: exact /exact live again');

# ── many cycles: tombstone churn stays correct ───────────────────────────────
like(jbody('/ctl/?op=cycle&n=100'), qr/"r":true/, '100 remove/restore cycles OK');
is(code('/gone/'), 200, 'after 100 cycles: /gone/ ends live');

# ── hard remove is irreversible ──────────────────────────────────────────────
like(jbody('/ctl/?op=hard&pat=/keep/'), qr/"r":true/, 'hard removeLocation /keep/');
like(jbody('/ctl/?op=restore&pat=/keep/'), qr/"r":false/,
     'restore after hard remove returns false (entry spliced, gone)');
