#!/usr/bin/perl

# COMCON increment C (C3-types): type-checking the tenant fragment against the
# C2 schema (schema/tenant-env.schema.json). Beyond C3.0 (names resolve) and
# C3-rest (no dynamic code), the typed profile checks that the tenant USES the
# environment at its declared type. This slice enforces the two statically-and-
# runtime-evident contracts:
#
#   (env.onRequest signature) the handler is (Request) => Response — a function
#   of at most one parameter, registered exactly once; and
#   (types.Request, sealed) the handler's Request parameter has only the fields
#   method/uri/args/headers — a direct read of any other field is refused.
#
# Aliased/interprocedural access and the Response/Socket typing are the
# erasure-complete remainder (deferred to C5); this check is a sound rejecter
# (no false positives), so those cases are admitted here.

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

# Clean: reads only schema Request fields, one-arg handler. Admitted.
$t->write_file('tenant.js', <<'JS');
onRequest(function(req) {
    return req.method + " " + req.uri + " " + req.args + "\n";
});
JS

$t->try_run('no js module')->plan(9);

# --- clean fragment (all four schema fields) serves ---
like(http_get('/t'), qr/GET \/t/, 'schema-typed handler admitted and serves');

my $dir = $t->testdir();
my $bin = $ENV{TEST_NGINX_BINARY} || 'nginx';

sub try_tenant {
    my ($tag, $body) = @_;
    open my $j, '>', "$dir/$tag.js" or die;
    print $j $body;
    close $j;
    open my $c, '>', "$dir/$tag.conf" or die;
    print $c "daemon off;\npid $dir/$tag.pid;\nerror_log $dir/$tag.log;\n";
    print $c "js_tenant_source $dir/$tag.js;\n";
    print $c "events { }\nhttp { access_log off; server { listen 127.0.0.1:8092; location /t { js_tenant_handler; } } }\n";
    close $c;
    return `$bin -t -p $dir -c $dir/$tag.conf 2>&1`;
}

# --- reject: a field the sealed Request type does not have ---
my $o1 = try_tenant('bad_field',
    "onRequest(function(req){ return req.secret; });\n");
like($o1, qr/non-schema Request field "secret"/,
     'reject: direct read of an undeclared Request field');
like($o1, qr/Request is sealed/, 'refusal cites the sealed Request type');

# --- reject: a different undeclared field, mixed with a valid one ---
my $o2 = try_tenant('bad_field2',
    "onRequest(function(req){ return req.method + req.cookies; });\n");
like($o2, qr/non-schema Request field "cookies"/,
     'reject: undeclared field caught even alongside a valid read');

# --- reject: handler arity > 1 (schema onRequest is (Request) => Response) ---
my $o3 = try_tenant('bad_arity',
    "onRequest(function(req, extra){ return req.uri; });\n");
like($o3, qr/at most one argument/,
     'reject: handler declaring more than the Request parameter');

# --- reject: registering the handler twice ---
my $o4 = try_tenant('bad_twice',
    "onRequest(function(req){ return 'a'; });\n"
  . "onRequest(function(req){ return 'b'; });\n");
like($o4, qr/already registered/,
     'reject: a second onRequest registration');

# --- admit: a zero-argument handler (ignores the Request) is in-contract ---
my $o5 = try_tenant('ok_noarg',
    "onRequest(function(){ return 'hi'; });\n");
like($o5, qr/test is successful/,
     'admit: a zero-parameter handler is within the (Request) => Response type');

# --- admit: reading req.headers (a schema field) then any key off it ---
my $o6 = try_tenant('ok_headers',
    "onRequest(function(req){ return String(req.headers['x-custom']); });\n");
like($o6, qr/test is successful/,
     'admit: req.headers is a schema field; keys off the Headers record are open');

# --- admit (known gap): aliased access is not yet caught (sound rejecter) ---
my $o7 = try_tenant('ok_alias_gap',
    "onRequest(function(req){ var r = req; return String(r.secret); });\n");
like($o7, qr/test is successful/,
     'admit: aliased field access is the deferred interprocedural remainder');
