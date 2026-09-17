#!/usr/bin/perl

# COMCON v5.127 -- SHOWCASE-gaps G-16: comcon.std.docs.model / .render -- the
# per-tenant reference manual as a QUERY over the contract a binding holds.
#
# SHOWCASE 29: "if it's in the docs, it works; if it works, it's in the docs".
# The renderer reads the same descriptors the kernel enforces (polOf's grant
# translation, the meter words, the admission fields) and nothing else, so the
# two are projections of one thing.  model() is the JSON; render() is the same
# as Markdown; ops.docs(name) renders a registered binding with its epoch.
#
# NEGATIVE CONTROLS (run while writing; each restored):
#   - a grant kind rendered from a second mapping (kindName wrong for 3) ->
#     test 4 fails
#   - the redacted field listed anyway (maskFields ignored) -> test 2 fails

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
        location /docs { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var srv  = nginx.http.servers[0];

var contract = {
    imports: ["JSON", "s", "http", "out", "author"],
    grants: {
        s:      comcon.mediate(comcon.mediate(comcon.mediate(sock, comcon.redact(["fd", "listener"])),
                    comcon.ttl(3600)), comcon.uses("acme:s", 100, 60)),
        http:   comcon.mediate(srv, comcon.routes("/acme/*")),
        out:    comcon.mediate(comcon.mediate(nginx.outbound(), comcon.allowHosts("https://*.example.com")),
                    comcon.cosign({ key: "rotate", quorum: 2, within: 900, as: "alice" })),
        author: comcon.author({ subFragments: 4 }),
        gone:   comcon.mediate(sock, comcon.revoke())
    },
    meter: comcon.meter({ timeoutMs: 250, retainedBytes: 1048576 }),
    checkRequest: true,
    tests: "function(f){}",
    identity: "0123456789abcdef0123456789abcdef",
    deps: [{ name: "lib", path: "/nowhere", sha256: "fedcba9876543210" }],
    onViolation: "audit"
};

var target = locs.find(function (l) { return l.path === "/docs"; });
var h = comcon.bindAt(function () {}, comcon.quote("function(){ return 1; }"),
                      { imports: [], meter: comcon.meter({ timeoutMs: 300 }) });
var ops = comcon.std.ops({ bindings: true });
ops.register("plain", h, comcon.quote("function(){ return 1; }"));

target.handler = function (req) {
    var o = {};
    var m = comcon.std.docs.model("acme", contract);
    o.grants = m.grants;
    o.meter = m.meter;
    o.posture = m.posture;
    o.identity = m.identity;
    o.deps = m.deps;
    o.text = comcon.std.docs.render("acme", contract);

    /* a fragment's docs read its bounds from the result */
    var f = comcon.include("function(a){ return 1; }", { imports: [], meter: comcon.meter({ timeoutMs: 42 }) });
    o.fragDeadline = comcon.std.docs.model("f", f).meter.timeoutMs;

    /* a registered binding, with its epoch */
    h.replace(comcon.quote("function(){ return 2; }"));
    o.opsDocs = ops.docs("plain");

    /* no admission at all is said, not hidden */
    o.noAdmission = /admission is OFF/.test(comcon.std.docs.render("bare", {}));

    req.respond(200, {'content-type': 'application/json'}, JSON.stringify(o));
};
JS

$t->try_run('no js module')->plan(12);

my $b = http_get('/docs');

like($b, qr/\{"name":"s","kind":"socket","ops":\["address \(read\)","port \(read\)"\],"ttlSeconds":3600,"budget":"100 per 60 s \(key acme:s\)"\}/,
     'a socket grant: the fields the mask leaves, the lifetime, the budget');
unlike($b, qr/"fd \(read\)"/, 'the redacted fields are not documented, because they do not work');
like($b, qr/\{"name":"http","kind":"server facet","ops":\["paths\(\)","route","allowed\(path\)"\],"within":"\/acme\/\*"\}/,
     'a server facet: its three ops within its glob');
like($b, qr/\{"name":"out","kind":"outbound","ops":\["request\(url\)"\],"within":"https:\/\/\*\.example\.com","cosign":"quorum 2 within 900 s, as alice"\}/,
     'an outbound grant: request within the glob, the cosignature it needs');
like($b, qr/\{"name":"author","kind":"author","ops":\["include\(source, contract\)"\],"subFragments":4\}/,
     'an author grant: one op, the slot count');
like($b, qr/\{"name":"gone","kind":"revoked","ops":\[\]\}/, 'a revoked grant is documented as nothing');
like($b, qr/"meter":\{"timeoutMs":250,"memoryBytes":"default \(16 MB\)","retainedBytes":1048576\}/,
     'the bounds, with the defaults named as defaults');
like($b, qr/"posture":"audit","identity":"0123456789ab\.\.\.","deps":\[\{"name":"lib","sha256":"fedcba987654\.\.\."\}\]/,
     'posture, pin and pinned dependencies');
like($b, qr/"text":"# acme -- your available API\\nderived from your contract; posture: audit; profile: restrictive\\n\\n## Capabilities\\n- `s` -- socket: address \(read\), port \(read\)\\n  - expires 3600 s after it was granted\\n  - budget: 100 per 60 s \(key acme:s\)\\n- `http` -- server facet within `\/acme\/\*`: paths\(\), route, allowed\(path\)\\n- `out` -- outbound within `https:\/\/\*\.example\.com`: request\(url\)\\n  - needs a cosignature: quorum 2 within 900 s, as alice\\n- `author` -- author: include\(source, contract\)\\n  - may hold 4 sub-fragments at once\\n- `gone` -- revoked: nothing\\n\\n## Names you may mention\\n- imports: JSON, s, http, out, author; language intrinsics: allowed\\n\\n## Bounds\\n- deadline: 250 ms per invocation \(an abort is not catchable\)\\n- allocation: default \(16 MB\) bytes per invocation\\n- retained: 1048576 bytes across invocations\\n\\n## Admission\\n- request fields: checked against the sealed schema\\n- tests: run in the compartment before admission\\n- identity: pinned \(0123456789ab\.\.\.\)\\n- dependency `lib` pinned \(fedcba987654\.\.\.\)\\n"/,
     'the rendered manual, line by line');
like($b, qr/"fragDeadline":42/, "a fragment's docs read the bound the include result carries");
like($b, qr/"opsDocs":"# plain -- your available API\\nderived from your contract, epoch 1; posture: inherit/,
     'ops.docs renders a registered binding with its live epoch');
like($b, qr/"noAdmission":true/, 'a contract with no admission is documented as such, not hidden');
