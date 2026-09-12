#!/usr/bin/perl

# COMCON M-CFG step 2: the host-JS `comcon.admit(fn, contract)` gate.
#
# admit is the host-JS realization of the kernel `admit` operator (FOUNDATION
# §4) — the C3 static gate over a compiled fragment. This first slice is the
# gate only (no compartment/bind/lowering): it reuses the shipped C3 checks —
# no dynamic code, and every free-global name must appear in the contract's
# `imports` manifest (eval/Function/globalThis/... always denied). It returns
# { certified, reject? }.

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

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /admit { }
    }
}
EOF

# The host script (HOST_ROOT) registers a handler that exercises comcon.admit
# on a battery of fragments and returns the verdicts as JSON.
$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/admit") {
        locs[i].handler = function(req) {
            var r = {};
            r.clean    = comcon.admit(function(q){ return "ok"; },        {imports: []});
            r.nginxNo  = comcon.admit(function(q){ return nginx.version; },{imports: []});
            r.nginxYes = comcon.admit(function(q){ return nginx.version; },{imports: ["nginx"]});
            r.evalBad  = comcon.admit(function(q){ return eval("1+1"); },  {imports: []});
            r.jsonYes  = comcon.admit(function(q){ return JSON.stringify(q); }, {imports: ["JSON"]});
            r.notFn    = comcon.admit(42, {imports: []});
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(r));
        };
    }
}
JS

$t->try_run('no js module')->plan(6);

my $body = http_get('/admit');

like($body, qr/"clean":\{"certified":true\}/,
     'admit: a fragment with no free names is certified');
like($body, qr/"nginxNo":\{"certified":false,"reject":"free name not declared in imports: nginx"\}/,
     'admit: an ungranted free name (nginx) is rejected');
like($body, qr/"nginxYes":\{"certified":true\}/,
     'admit: the same name granted via imports is certified');
like($body, qr/"evalBad":\{"certified":false,"reject":"dynamic-code[^"]*"\}/,
     'admit: dynamic code (eval) is rejected');
like($body, qr/"jsonYes":\{"certified":true\}/,
     'admit: an intrinsic listed in imports is certified');
like($body, qr/"notFn":\{"certified":false,"reject":"admit: arg0 must be a function"\}/,
     'admit: a non-function arg is rejected');
