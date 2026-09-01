#!/usr/bin/perl

# COMCON increment C (C3): the static free-name admission check. The typed-
# profile front-end (first slice) walks the registered handler's free-name
# manifest (its closure_var globals, recursively) and REFUSES the fragment at
# LOAD if it references any name not in the tenant environment — fail fast,
# instead of throwing deep in a request (deny-by-default at runtime). A clean
# fragment (report / standard JS / grants only) is admitted and serves.

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

js_tenant_source %%TESTDIR%%/tenant.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        location /t { js_tenant_handler; }
    }
}
EOF

# Clean: references only report (granted) + standard JS (JSON). Admitted.
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    return "len=" + JSON.stringify({a: 1, b: 2}).length + " " + req.uri + "\n";
});
JS

$t->try_run('no js module')->plan(6);

# --- clean fragment is admitted and serves ---
like(http_get('/t'), qr/len=\d+ \/t/, 'clean fragment admitted and serves');

my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

# helper: write a tenant + minimal conf and return `nginx -t` output
sub try_tenant {
    my ($tag, $body) = @_;
    open my $j, '>', "$dir/$tag.js" or die;
    print $j $body;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die;
    print $c "daemon off;\npid $dir/$tag.pid;\nerror_log $dir/$tag.log;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8090; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- reject: handler references an ungranted host name ---
my $o1 = try_tenant('bad_nginx',
    "onRequest(function(req){ return String(nginx.foo); });\n");
like($o1, qr/ungranted name "nginx"/, 'reject: handler references nginx');
like($o1, qr/COMCON C3 admission/,     'refusal cites the C3 admission gate');

# --- reject: a different ungranted name (fetch) ---
my $o2 = try_tenant('bad_fetch',
    "onRequest(function(req){ return fetch('http://x/'); });\n");
like($o2, qr/ungranted name "fetch"/, 'reject: handler references fetch');

# --- reject: the ungranted name is inside a NESTED function (recursion) ---
my $o3 = try_tenant('bad_nested',
    "onRequest(function(req){ function inner(){ return createSocket('x'); } return inner(); });\n");
like($o3, qr/ungranted name "createSocket"/,
     'reject: nested-function reference is caught (recursive walk)');

# --- admit: fragment-local names (a declared helper) are NOT flagged ---
my $o4 = try_tenant('ok_local',
    "function helper(n){ return n*2; }\nonRequest(function(req){ return String(helper(21)); });\n");
like($o4, qr/test is successful/,
     'admit: a fragment-declared helper is not mistaken for an ungranted global');
