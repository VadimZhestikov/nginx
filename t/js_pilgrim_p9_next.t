#!/usr/bin/perl

# Tests for JS-Pilgrim P9: next() / middleware-style hook wrapping.
#
# location.addHook((req, next) => { ... await next(); ... })
# nginx.http.addHook((req, next) => { ... await next(); ... })
#
# Calling next() runs the remaining hooks (for P1 or P2) and returns a
# Promise that settles when they complete.  Code after `await next()` in
# a P1 hook runs BEFORE the content handler (which is not part of the
# chain).  Hooks that do not call next() auto-advance (backward compat).

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(20);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p9_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /p1_next/         { }
        location /p1_no_next/      { }
        location /p1_wrap/         { }
        location /p2_global_next/  { }
        location /p2_no_next/      { }
        location /p2_wrap/         { }
    }
}
EOF

# Location alphabetical order (nginx sorts by URI string):
#   locs[0] = /p1_next/
#   locs[1] = /p1_no_next/
#   locs[2] = /p1_wrap/
#   locs[3] = /p2_global_next/
#   locs[4] = /p2_no_next/
#   locs[5] = /p2_wrap/

$t->write_file_expand('p9_init.js', <<'JS');
// JS-Pilgrim P9 — next() middleware hook tests
//
// State stored in module-level variables (req wrappers are replaced on
// async re-entry, so req properties would be lost across await boundaries).

var locs = nginx.http.servers[0].locations;

// ----------------------------------------------------------------
// P1 tests — location-scope hooks
// ----------------------------------------------------------------

// /p1_next/ (locs[0]):
// Sync hook that explicitly calls next().  next() runs remaining hooks
// (none here) and returns a Promise that settles synchronously via microtask
// drain.  Code in .then() runs before the content handler.
var p1PreFlag  = false;
var p1PostFlag = false;

locs[0].addHook(function(req, next) {
    p1PreFlag = true;
    return next().then(function() {
        p1PostFlag = true;
    });
});

locs[0].handler = function(req) {
    var pre  = p1PreFlag;
    var post = p1PostFlag;
    p1PreFlag  = false;
    p1PostFlag = false;
    req.respond(200, {}, 'pre:' + (pre ? '1' : '0')
                       + ',post:' + (post ? '1' : '0') + '\n');
};

// /p1_no_next/ (locs[1]):
// Hook that does NOT call next() — auto-advances to the handler.
var p1NoNextFlag = false;

locs[1].addHook(function(req) {
    p1NoNextFlag = true;
    // no next() — chain auto-advances
});

locs[1].handler = function(req) {
    var v = p1NoNextFlag;
    p1NoNextFlag = false;
    req.respond(200, {}, 'flag:' + (v ? '1' : '0') + '\n');
};

// /p1_wrap/ (locs[2]):
// Async hook that awaits both before and after next().
// Execution order: A (sync) → B (after setTimeout) → D (after next) → handler: C.
// The handler runs after the chain settles, so response log = "ABDC".
var p1WrapLog = '';

locs[2].addHook(async function(req, next) {
    p1WrapLog += 'A';
    await nginx.setTimeout(2);
    p1WrapLog += 'B';
    await next();
    p1WrapLog += 'D';
});

locs[2].handler = function(req) {
    p1WrapLog += 'C';
    var v = p1WrapLog;
    p1WrapLog = '';
    req.respond(200, {}, 'log:' + v + '\n');
};

// ----------------------------------------------------------------
// P2 tests — global-scope hooks
// ----------------------------------------------------------------

// Global hook 0: sync, guards /p2_global_next/ only.
// pre flag set; after next() (sync microtask) post flag also set — both
// visible to the handler.
var p2PreFlag  = false;
var p2PostFlag = false;

nginx.http.addHook(function(req, next) {
    if (req.uri.indexOf('/p2_global_next/') !== 0) return;
    p2PreFlag = true;
    return next().then(function() {
        p2PostFlag = true;
    });
});

// Global hook 1: async wrap for /p2_wrap/.  Same ABDC order as P1 wrap.
var p2WrapLog = '';

nginx.http.addHook(async function(req, next) {
    if (req.uri.indexOf('/p2_wrap/') !== 0) return;
    p2WrapLog += 'A';
    await nginx.setTimeout(2);
    p2WrapLog += 'B';
    await next();
    p2WrapLog += 'D';
});

// Global hook 2: no next() call for /p2_no_next/ — auto-advances.
var p2NoNextFlag = false;

nginx.http.addHook(function(req) {
    if (req.uri.indexOf('/p2_no_next/') !== 0) return;
    p2NoNextFlag = true;
    // no next()
});

// /p2_global_next/ handler (locs[3])
locs[3].handler = function(req) {
    var pre  = p2PreFlag;
    var post = p2PostFlag;
    p2PreFlag  = false;
    p2PostFlag = false;
    req.respond(200, {}, 'pre:' + (pre ? '1' : '0')
                       + ',post:' + (post ? '1' : '0') + '\n');
};

// /p2_no_next/ handler (locs[4])
locs[4].handler = function(req) {
    var v = p2NoNextFlag;
    p2NoNextFlag = false;
    req.respond(200, {}, 'flag:' + (v ? '1' : '0') + '\n');
};

// /p2_wrap/ handler (locs[5])
locs[5].handler = function(req) {
    p2WrapLog += 'C';
    var v = p2WrapLog;
    p2WrapLog = '';
    req.respond(200, {}, 'log:' + v + '\n');
};
JS

$t->run();

# -----------------------------------------------------------------------
# 1–4: P1 sync hook with explicit next()
#   pre:1  — hook ran before next()
#   post:1 — .then() settled via microtask drain before content handler
# -----------------------------------------------------------------------

my $r = http_get('/p1_next/');
like($r, qr{200 OK},           'P1 next: 200 OK');
like($r, qr{pre:1},            'P1 next: pre-hook ran');
like($r, qr{post:1},           'P1 next: post-next code ran before handler');
like($r, qr{pre:1,post:1},     'P1 next: both flags set when handler reads them');

# -----------------------------------------------------------------------
# 5–6: P1 hook without next() — auto-advances to handler
# -----------------------------------------------------------------------

$r = http_get('/p1_no_next/');
like($r, qr{200 OK},           'P1 no-next: 200 OK');
like($r, qr{flag:1},           'P1 no-next: hook ran, handler reached');

# -----------------------------------------------------------------------
# 7–10: P1 async wrap: A→B (before+after setTimeout), D (after next),
#        C (in handler) — handler runs AFTER chain, so D precedes C
# -----------------------------------------------------------------------

$r = http_get('/p1_wrap/');
like($r, qr{200 OK},           'P1 wrap: 200 OK');
like($r, qr{log:ABDC},         'P1 wrap: A+B in hook, D after next(), C in handler');

# -----------------------------------------------------------------------
# 9–12: P2 global sync hook with next() — same semantics as P1
# -----------------------------------------------------------------------

$r = http_get('/p2_global_next/');
like($r, qr{200 OK},           'P2 global next: 200 OK');
like($r, qr{pre:1},            'P2 global next: pre-hook ran');
like($r, qr{post:1},           'P2 global next: post-next code ran');
like($r, qr{pre:1,post:1},     'P2 global next: both flags visible to handler');

# -----------------------------------------------------------------------
# 13–14: P2 global hook without next() — auto-advances to content phase
# -----------------------------------------------------------------------

$r = http_get('/p2_no_next/');
like($r, qr{200 OK},           'P2 no-next: 200 OK');
like($r, qr{flag:1},           'P2 no-next: hook ran, handler reached');

# -----------------------------------------------------------------------
# 15–16: P2 global async wrap
# -----------------------------------------------------------------------

$r = http_get('/p2_wrap/');
like($r, qr{200 OK},           'P2 wrap: 200 OK');
like($r, qr{log:ABDC},         'P2 wrap: A+B in hook, D after next(), C in handler');

# -----------------------------------------------------------------------
# 17–18: Repeated requests — state reset per-request
# -----------------------------------------------------------------------

$r = http_get('/p1_next/');
like($r, qr{pre:1,post:1},     'P1 next: second request ok');

$r = http_get('/p2_global_next/');
like($r, qr{pre:1,post:1},     'P2 global next: second request ok');

# -----------------------------------------------------------------------
# 19–22: Auto checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
