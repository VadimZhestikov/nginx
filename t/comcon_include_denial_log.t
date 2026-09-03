#!/usr/bin/perl

# COMCON CONVERGE P4: the denial-log / TM-1 quota scenario of comcon_denial_log.t,
# re-expressed on include. A confined include fragment holding a live-cap grant
# hits the A1 reach gate 250x (granted.listener stays null under enforce); the
# shared denial machinery counts them exactly and logs 100 full records + a 1/100
# sample. nginx.tenantDenials() reports the same counters — the denial subsystem
# is shared with the tenant path, now exercised through include.

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

        location /spin    { }
        location /denials { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
nginx.http.attach(sock).addServer(nginx.http.servers[0]);

var spin = comcon.include(
    "function(req){" +
    "  var nulls=0, objs=0;" +
    "  for (var i=0;i<250;i++){ if(granted.listener===null){nulls++;}else{objs++;} }" +
    "  return { status:200, body:'nulls='+nulls+' objs='+objs }; }",
    { grants: { granted: sock } });

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/spin") {
        locs[i].handler = function(req) {
            var o = spin({});
            req.respond(o.status, {'content-type':'text/plain'}, o.body);
        };
    }
    if (locs[i].path === "/denials") {
        locs[i].handler = function(req) {
            req.respond(200, {'content-type':'application/json'},
                        JSON.stringify(nginx.tenantDenials()));
        };
    }
}
JS

$t->try_run('no js module')->plan(7);

like(http_get('/spin'), qr/nulls=250 objs=0/,
     'enforcement held for all 250 gated reads in the confined fragment');

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
