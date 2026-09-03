#!/usr/bin/perl

# COMCON CONVERGE P6 (start): the js_tenant_* directives are DEPRECATED. They
# still work (thin sugar over the same jcf fields the operators set), but each
# logs a config-time deprecation warning pointing to its host-JS replacement:
#   js_tenant_source     -> comcon.tenant(path)
#   js_tenant_mode       -> comcon.mode(...)
#   js_tenant_handler    -> location.handler = comcon.include(...) callable
# Full removal is gated on P5 (the CONFIG_JIT compiled tier still lowers the
# tenant onRequest handler).

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

js_source        %%TESTDIR%%/host.js;
js_tenant_source %%TESTDIR%%/tenant.js;
js_tenant_mode   audit;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /t { js_tenant_handler; }
    }
}
EOF

$t->write_file('host.js', "nginx.log(6, 'host up');\n");
$t->write_file('tenant.js', "onRequest(function(req){ return \"ok\\n\"; });\n");

$t->try_run('no js module')->plan(4);

# the tenant path still works (deprecated != removed)
like(http_get('/t'), qr/ok/, 'deprecated directives still function (thin sugar)');

my $log = $t->read_file('error.log');
like($log, qr/"js_tenant_source" is deprecated.*comcon\.tenant/,
     'js_tenant_source logs a deprecation warning -> comcon.tenant()');
like($log, qr/"js_tenant_mode" is deprecated.*comcon\.mode/,
     'js_tenant_mode logs a deprecation warning -> comcon.mode()');
like($log, qr/"js_tenant_handler" is deprecated.*location\.handler/,
     'js_tenant_handler logs a deprecation warning -> location.handler + comcon.include');
