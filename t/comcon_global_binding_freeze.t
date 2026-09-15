#!/usr/bin/perl

# F15, PHASE 1 — a fragment cannot reassign a shared global for every OTHER
# fragment.
#
# The compartment context is ONE runtime and ONE global object shared by every
# fragment for the life of the worker.  M-SES-1 already freezes the intrinsic
# VALUES (Object.prototype, Array.prototype, ...) so a tenant cannot pollute a
# shared prototype -- but it deliberately never froze globalThis itself, and
# that left every BINDING writable: the name `Promise` pointing at `Promise`,
# not `Promise`'s own properties.
#
# MEASURED, before this fix: an ordinary, PROPERLY ADMITTED fragment body --
# `imports: ['Promise']`, nothing exotic -- did `Promise = function(){ return
# 'EVIL'; }`, and every OTHER fragment that read `Promise` afterwards got the
# attacker's function.  `imports` governs whether a name may be REFERENCED at
# all; it was never asked whether the reference was a read or a write, and
# admission's own "INTRINSIC" category is spelled "a value to compute with,
# not authority it acts through" -- true of READING Math or JSON, false of
# REASSIGNING them for every co-resident tenant.  The same failure reached
# UN-ADMITTED fragments too: `comcon.include(src, {})` skips admission
# entirely by design, so `JSON = {...}` needed no declaration at all.
#
# THE FIX is a runtime, value-level protection -- freeze every binding present
# on globalThis at the moment the compartment is built, before any dependency
# or fragment has ever run -- so it closes both paths (admitted and
# un-admitted) with one mechanism, orthogonal to whether admission ran at all.
#
# WHAT THIS DOES NOT CLOSE, recorded rather than silently assumed: a fragment
# that explicitly imports `globalThis` and plants a brand-new name on it as a
# rendezvous with another fragment.  Freezing protects an EXISTING binding, not
# a NEW one.  `globalThis` (like `eval`/`Function`/`self`) is already denied by
# admission even when listed in `imports` (test 6 below, and
# `t/comcon_include_admit.t`), so that channel does not open through admission
# -- it remains open for UN-ADMITTED fragments, which have no free-name gate at
# all by design.  Closing that needs an architectural change (a private scope
# per fragment), not this patch.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

worker_processes 1;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /freeze { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var l = nginx.http.servers[0].locations[0];

function attempt(o, name, src, contract) {
    try {
        var f = comcon.include(src, contract);
        o[name] = { included: true, result: f({}) };
    } catch (e) {
        o[name] = { refused: String(e && e.message).substring(0, 140) };
    }
}

l.handler = function (req) {
    var o = {};

    /* THE ORIGINAL ESCAPE, admitted: declaring `Promise` for READ purposes
     * (the only reason `imports` documents) also let this body WRITE it. */
    attempt(o, 'admittedOverwrite',
        "function(a){ try { Promise = function(){ return 'EVIL'; };"
      + " return 'assigned'; } catch (e) { return 'threw: ' + e.message; } }",
        { imports: ['Promise'] });

    /* A DIFFERENT fragment, reading the SAME binding afterwards. */
    attempt(o, 'admittedVictim',
        "function(a){ return String(Promise).substring(0, 24); }",
        { imports: ['Promise'] });

    /* THE UN-ADMITTED PATH: {} skips admission entirely (documented,
     * backward-compatible), so there was never a free-name gate to defeat --
     * the freeze is the only thing standing here. */
    attempt(o, 'unadmittedOverwrite',
        "function(a){ try { JSON = { stringify: function(){"
      + " return 'EVIL-JSON'; } }; return 'assigned';"
      + " } catch (e) { return 'threw: ' + e.message; } }", {});

    attempt(o, 'unadmittedVictim',
        "function(a){ return JSON.stringify({}); }", {});

    /* ORDINARY USE MUST BE UNCHANGED: reading and computing with a frozen
     * intrinsic works exactly as before -- only reassignment is refused. */
    attempt(o, 'ordinaryRead', "function(a){ return Math.max(1,2,3); }",
        { imports: ['Math'] });
    attempt(o, 'ordinaryTypeof', "function(a){ return typeof Promise; }",
        { imports: ['Promise'] });

    /* THE RESIDUAL, pinned rather than ignored: globalThis stays out of
     * reach through admission (unrelated to this fix -- it is on the
     * pre-existing DENY list), so this must still be refused. */
    attempt(o, 'globalThisImport', "function(a){ return typeof globalThis; }",
        { imports: ['globalThis'] });

    req.respond(200, { 'content-type': 'application/json' }, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(8);

sub get_json {
    my ($path) = @_;
    my $raw = http_get($path);
    $raw =~ s/^.*?\r\n\r\n//s;
    my $o;
    eval { $o = decode_json($raw); 1 } or do {
        diag("non-JSON from $path: " . substr($raw, 0, 400)); $o = {};
    };
    return $o;
}

my $o = get_json('/freeze');

like($o->{admittedOverwrite}{result} // '', qr/^threw: .*read-only/,
   'AN ADMITTED FRAGMENT CANNOT REASSIGN A DECLARED INTRINSIC: `imports` says '
   . 'a name may be REFERENCED, not that it may be OVERWRITTEN for every other '
   . 'fragment. Before the fix this was "assigned"')
    or diag('admittedOverwrite: ' . encode_json($o->{admittedOverwrite} || {}));

like($o->{admittedVictim}{result} // '', qr/^function Promise\(\) \{/,
   "...AND THE VICTIM STILL SEES THE REAL Promise. Before the fix this read "
   . "\"function(){ return 'EVIL'; }\"")
    or diag('admittedVictim: ' . encode_json($o->{admittedVictim} || {}));

like($o->{unadmittedOverwrite}{result} // '', qr/^threw: .*read-only/,
   'AND AN UN-ADMITTED FRAGMENT CANNOT EITHER: comcon.include(src, {}) skips '
   . 'admission entirely by design, so there was never a free-name gate here '
   . 'at all -- the freeze is what stops it. Before the fix this was "assigned"')
    or diag('unadmittedOverwrite: ' . encode_json($o->{unadmittedOverwrite} || {}));

is($o->{unadmittedVictim}{result}, '{}',
   "...and the victim's JSON.stringify still behaves. Before the fix this "
   . "read \"EVIL-JSON\"")
    or diag('unadmittedVictim: ' . encode_json($o->{unadmittedVictim} || {}));

is($o->{ordinaryRead}{result}, 3,
   'ORDINARY READS OF A FROZEN INTRINSIC ARE UNCHANGED: Math.max still computes');

is($o->{ordinaryTypeof}{result}, 'function',
   '...and referencing Promise itself (not reassigning it) still works');

like($o->{globalThisImport}{refused} // '', qr/free name not declared in imports: globalThis/,
   'THE RESIDUAL IS UNCHANGED BY THIS FIX, NOT WIDENED: globalThis stays '
   . 'refused by the pre-existing admission deny-list -- this freeze protects '
   . 'EXISTING bindings, not a NEW name planted through an explicit '
   . 'globalThis import, and that gap is recorded, not fixed here')
    or diag('globalThisImport: ' . encode_json($o->{globalThisImport} || {}));

unlike($t->read_file('error.log'), qr/\[emerg\]|\[alert\]/,
   'no alerts: freezing did not destabilize the compartment');
