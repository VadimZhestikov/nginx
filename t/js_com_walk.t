#!/usr/bin/perl

# COM tree traversal safety, across every phase the tree is reachable in.
#
# This is a MEMORY-SAFETY smoke test, not a semantics test: it reads every
# reachable COM member and asserts only that nothing faults and nothing throws.
# The registry-coverage invariants (every method classified, every classified
# method typed) live in t/js_com_describe.t; this file is about whether the
# getters survive being called at all.
#
# It exists because ordinary tests touch only the handful of members they
# assert on, so no test had ever read EVERY getter. Walking the whole tree
# immediately faulted nginx at config phase in the location.charset getter,
# which reached for the global ngx_cycle -- still the OLD cycle during
# ngx_js_init_conf, with no http main conf on a fresh start.
#
# The phases matter independently, because the cycle a getter should consult
# differs in each:
#
#   config phase   ngx_cycle is the OLD cycle; must use ngx_js_conf_cycle()
#   request phase  the JSContext opaque is ngx_js_worker_t*, NOT a cycle
#   after SIGHUP   a pointer captured at the previous init_conf is now stale
#
# A regression in any one of them is a worker crash reachable from host JS --
# and under COMCON, from a confined tenant enumerating its own grants.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy upstream_zone stream stream_return/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream zoned { zone zoned 64k; server 127.0.0.1:%%PORT_8091%%; }
    upstream plain { server 127.0.0.1:%%PORT_8092%%; }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /      { }
        location /p/    { proxy_pass http://zoned; }
        location /walk  { }
    }
}

stream {
    upstream sup { zone sup 64k; server 127.0.0.1:%%PORT_8093%%; }

    server {
        listen      127.0.0.1:%%PORT_8094%%;
        proxy_pass  sup;
    }

    server {
        listen  127.0.0.1:%%PORT_8095%%;
        return  "ok\n";
    }
}
EOF

$t->write_file('init.js', <<'JS');
/*
 * Read every reachable COM member.  Bounded by DEPTH and node count, never by
 * object identity: COM wrappers are rebuilt per access (proto-per-call), so a
 * visited-set never converges.
 *
 * Every read is wrapped.  A throw is recorded rather than propagated -- an
 * unreachable member is worth reporting, but it is a fault that takes the
 * process down, and that is what this walk is for.
 */
function walkCom(root, path) {
    var st = { walked: 0, threw: [] };

    function step(obj, p, depth) {
        if (depth > 6 || st.walked > 400 || !obj || typeof obj !== 'object') {
            return;
        }
        st.walked++;

        var ds;
        try { ds = nginx.describe(obj); }
        catch (e) { st.threw.push(p + ' describe: ' + e); return; }
        if (!ds || typeof ds.length !== 'number') { return; }

        /* describe() reports classified members plus prototype getters only,
         * so plain-object nodes (nginx.http) hide their children from it --
         * walk own keys too, or the traversal never leaves the root. */
        var names = {}, k;
        for (k = 0; k < ds.length; k++) {
            if (ds[k].callable !== true) { names[ds[k].name] = 1; }
        }
        var own;
        try { own = Object.keys(obj); } catch (e) { own = []; }
        for (k = 0; k < own.length; k++) { names[own[k]] = 1; }

        for (var nm in names) {
            var v;
            try { v = obj[nm]; }
            catch (e) { st.threw.push(p + '.' + nm + ': ' + e); continue; }

            if (!v || typeof v !== 'object') { continue; }

            if (typeof v.length === 'number' && v.length > 0
                && typeof v[0] === 'object')
            {
                for (var j = 0; j < v.length && j < 2; j++) {
                    step(v[j], p + '.' + nm + '[' + j + ']', depth + 1);
                }
            } else {
                step(v, p + '.' + nm, depth + 1);
            }
        }
    }

    step(root, path, 0);
    return st;
}

function pass(name) { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + got); }
function check(name, ok, got) {
    if (ok) { pass(name); } else { fail(name, String(got)); }
}

/* ---- config phase ------------------------------------------------------- */
var ch = walkCom(nginx.http, 'http');
check('cfg_http_reached',  ch.walked > 100, 'walked=' + ch.walked);
check('cfg_http_no_throw', ch.threw.length === 0, ch.threw.join(' | '));

var cs = walkCom(nginx.stream, 'stream');
check('cfg_stream_reached',  cs.walked > 10, 'walked=' + cs.walked);
check('cfg_stream_no_throw', cs.threw.length === 0, cs.threw.join(' | '));

/* ---- request phase: same tree, different cycle discipline --------------- */
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/walk") {
        locs[i].handler = function (req) {
            var h = walkCom(nginx.http,   'http');
            var s = walkCom(nginx.stream, 'stream');
            req.respond(200, {"Content-Type":"application/json"},
                JSON.stringify({
                    httpWalked:  h.walked,
                    streamWalked: s.walked,
                    threw: h.threw.concat(s.threw)
                }));
        };
    }
}
JS

# run(), NOT try_run(): try_run() swallows a startup failure into skip_all, and
# prove counts a skipped file as SUCCESS -- so the very crash this file exists
# to catch would pass CI silently (verified: reverting the charset fix turns
# try_run into "skipped: no js or stream module", Result NOTESTS, and a full
# `prove t/` stays green). has() above already covers the legitimate skip, when
# a module genuinely is not compiled in; anything past that is a real failure.
$t->run()->plan(12);

###############################################################################

# --- config phase (error.log) ---
my $log = $t->read_file('error.log');
like($log, qr/JSTEST PASS cfg_http_reached/,
     'config phase: http COM tree fully walked (no SIGSEGV)');
like($log, qr/JSTEST PASS cfg_http_no_throw/,
     'config phase: no http getter throws');
like($log, qr/JSTEST PASS cfg_stream_reached/,
     'config phase: stream COM tree fully walked');
like($log, qr/JSTEST PASS cfg_stream_no_throw/,
     'config phase: no stream getter throws');

# --- request phase ---
my $r = http_get('/walk');
like($r, qr/"httpWalked":\d\d\d/,
     'request phase: http COM tree fully walked (worker survived)');
like($r, qr/"streamWalked":[1-9]\d/,
     'request phase: stream COM tree fully walked');
like($r, qr/"threw":\[\]/,
     'request phase: no getter throws');

# --- after reload: a pointer captured at the previous init_conf is stale ---
$t->reload();
select undef, undef, undef, 1.5;
my $r2 = http_get('/walk');
like($r2, qr/"httpWalked":\d\d\d/,   'after reload #1: http tree still walkable');
like($r2, qr/"threw":\[\]/,          'after reload #1: no getter throws');

$t->reload();
select undef, undef, undef, 1.5;
my $r3 = http_get('/walk');
like($r3, qr/"httpWalked":\d\d\d/,   'after reload #2: http tree still walkable');
like($r3, qr/"threw":\[\]/,          'after reload #2: no getter throws');

# A fault during any walk kills the worker and the response never arrives, so
# the request assertions above are the real crash detector; this catches the
# quieter failure where a getter logs an error but returns.
unlike($t->read_file('error.log'), qr/\[(?:alert|emerg)\]/,
     'no alerts or emergencies across all phases');
