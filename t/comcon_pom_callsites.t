#!/usr/bin/perl

# COMCON increment D5a — call-site enumeration from bytecode (no parser). The
# audit/query READ side of SHOWCASE §38: node.references(name) enumerates every
# reference to a free name or method `name` in a fragment (with line numbers),
# and node.callsites(name) is the subset that are actual CALL sites. Callee↔call
# correlation is exact (the operand stack is tracked, so a nested-argument call
# like fetch(helper(2)) is still attributed to fetch). The ENFORCEMENT side is
# the capability kernel (mediate a granted name); this is the "where is it used"
# half.

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
        location /cs { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/cs") {
        locs[i].handler = function(req) {
            var out = {};

            var frag = function(x) {
                var a = fetch(1);           // callsite fetch (call, !method)
                var b = fetch(helper(2));   // callsite fetch with a NESTED arg
                var g = fetch;              // reference fetch (NOT a call)
                var c = obj.write(3);       // callsite write (call, method)
                var d = obj.write;          // reference write (NOT a call)
                return a + b + g + c + d + x;
            };

            var node = comcon.pom(frag);

            var fref = node.references("fetch");
            var fcs  = node.callsites("fetch");
            out.fetchRefs  = fref.length;                 // 3
            out.fetchCalls = fcs.length;                  // 2
            out.fetchAllCalls  = fcs.every(function(r){ return r.call === true; });
            out.fetchNotMethod = fcs.every(function(r){ return r.method === false; });
            out.fetchLines = fcs.map(function(r){ return r.line; })
                                .sort().join(",");        // two distinct lines
            out.nestedDetected = (fcs.length === 2);      // helper(2) didn't desync

            var wcs  = node.callsites("write");
            var wref = node.references("write");
            out.writeCalls   = wcs.length;                // 1
            out.writeMethod  = wcs.length === 1 && wcs[0].method === true;
            out.writeRefs    = wref.length;               // 2 (call + bare ref)

            out.helperCalls  = node.callsites("helper").length;  // 1
            out.absentCalls  = node.callsites("neverUsed").length; // 0

            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(out));
        };
    }
}
JS

$t->try_run('no js module')->plan(11);

my $body = http_get('/cs');

like($body, qr/"fetchRefs":3/,       'references(fetch): 3 references (2 calls + 1 bare ref)');
like($body, qr/"fetchCalls":2/,      'callsites(fetch): 2 actual call sites');
like($body, qr/"fetchAllCalls":true/,'every fetch callsite is flagged call=true');
like($body, qr/"fetchNotMethod":true/,'fetch callsites are free-name (method=false)');
like($body, qr/"nestedDetected":true/,'nested-arg call fetch(helper(2)) still attributed to fetch');
like($body, qr/"writeCalls":1/,      'callsites(write): 1 method call site');
like($body, qr/"writeMethod":true/,  'the write callsite is a method (method=true)');
like($body, qr/"writeRefs":2/,       'references(write): 2 (the call + the bare obj.write)');
like($body, qr/"helperCalls":1/,     'callsites(helper): 1 (the nested call)');
like($body, qr/"absentCalls":0/,     'callsites of an unused name: empty');
like($body, qr/HTTP\/1\.1 200/,      'handler responded 200');
