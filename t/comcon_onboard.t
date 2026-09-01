#!/usr/bin/perl

# COMCON increment B (B1): generated grant-stub docs. In learn mode the tenant
# reaches for host surface it was not granted; the host feeds
# nginx.tenantLearning() through a generator (plain library JS) to produce a
# paste-ready onboarding contract — each wanted path classified refuse/review,
# the wanted-vs-granted delta, and the js_tenant_mode enforce next step.
#
# Also guards the tenantLearning()-from-a-worker path (grants list): the report
# must read jcf via ngx_cycle, not the worker's context opaque.

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

js_tenant_mode   learn;
js_source        %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /go       { js_tenant_handler; }
        location /contract { }
    }
}
EOF

# Host: create + grant a socket (exercises the grants list), then serve the
# generated onboarding contract. Generator inlined (it is plain library JS).
$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.grantToTenant("edge", sock);

var CLASS = { createSocket:"REFUSE", Worker:"REFUSE", config:"REFUSE",
              use:"REFUSE", install:"REFUSE", nginx:"REVIEW", fetch:"REVIEW" };
function classify(p){ var t=String(p).split(/[.(]/)[0]; return CLASS[t]||"REVIEW"; }
function generateContract(L){
    var w=(L&&L.wants)||[], g=(L&&L.grants)||[], out=[], nR=0;
    out.push("# mode observed: "+(L?L.mode:"?"));
    out.push("# granted now: "+(g.length?g.join(", "):"(none)"));
    for (var i=0;i<w.length;i++){ var v=classify(w[i].path); if(v==="REFUSE")nR++;
        out.push("#   ["+v+"] "+w[i].path+" (x"+w[i].hits+")"); }
    out.push("# Summary: "+nR+" to refuse.");
    out.push("js_tenant_mode enforce;");
    return out.join("\n")+"\n";
}

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/contract") {
        locs[i].handler = function(req){
            req.respond(200, {'content-type':'text/plain'},
                        generateContract(nginx.tenantLearning()));
        };
    }
}
JS

$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    nginx.http.addServer({});
    createSocket("127.0.0.1:9");
    fetch("http://x/");
    return "ok\n";
});
JS

$t->try_run('no js module')->plan(6);

like(http_get('/go'), qr/ok/, 'tenant runs in learn mode');

my $c = http_get('/contract');

like($c, qr/mode observed: learn/,        'generated: records learn mode');
like($c, qr/granted now: edge/,
     'generated: lists granted names (tenantLearning grants list, from worker)');
like($c, qr/\[REFUSE\] createSocket\(\)/, 'generated: createSocket -> REFUSE');
like($c, qr/\[REVIEW\] nginx\.http\.addServer/, 'generated: nginx.* -> REVIEW');
like($c, qr/js_tenant_mode enforce;/,     'generated: emits the enforce step');
