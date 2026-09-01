#!/usr/bin/perl

# COMCON increment A (A4): the denial log with TM-1 quotas + the host-side
# report. A tenant loops 250 times on a gated reach edge (granted socket's
# .listener) in one request: enforcement holds for every iteration, the
# per-code counters stay EXACT (nginx.tenantDenials() → 250), while the log
# is bounded per TM-1 — 100 full records, then sampling 1/100 (exactly one
# sampled record at n=200), with quota-exceeded itself reported once. A
# denial-looping tenant cannot exhaust disk or drown the audit signal.

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
js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;

        location /spin    { js_tenant_handler; }
        location /denials { }
    }
}
EOF

$t->write_file_expand('host.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(nginx.http.servers[0]);
nginx.grantToTenant("granted", sock);

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/denials") {
        locs[i].handler = function(req) {
            req.respond(200, {'content-type': 'application/json'},
                        JSON.stringify(nginx.tenantDenials()));
        };
    }
}
JS

$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    var nulls = 0, objs = 0;
    for (var i = 0; i < 250; i++) {
        if (granted.listener === null) { nulls++; } else { objs++; }
    }
    return "nulls=" + nulls + " objs=" + objs;
});
JS

$t->try_run('no js module')->plan(7);

like(http_get('/spin'), qr/nulls=250 objs=0/,
     'enforcement held for all 250 gated reads');

my $rep = http_get('/denials');
like($rep, qr/"mode":"enforce"/, 'report: enforce mode');
like($rep, qr/"total":250/, 'report: counters exact despite log quota');
like($rep, qr/"sock\.listener":250/, 'report: exact per-op count');

my $log = $t->read_file('error.log');

my $records = () = $log =~ /js denial: comp=1 op=/g;
is($records, 101, 'TM-1: 100 full records + exactly 1 sampled (of 250)');

like($log, qr/mode=enforce n=200 sampled=1/,
     'TM-1: the sampled record is the 200th denial (1/100 above quota)');

my $notices = () = $log =~ /quota exceeded/g;
is($notices, 1, 'TM-1: quota-exceeded reported exactly once');
