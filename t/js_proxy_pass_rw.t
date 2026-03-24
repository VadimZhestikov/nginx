#!/usr/bin/perl

# Tests for location.proxy.pass (r/w):
#   read    — initial value from proxy_pass directive
#   set     — switch to a different named upstream
#   persist — change survives a second request

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream alpha {
        server 127.0.0.1:%%PORT_8091%%;
    }

    upstream beta {
        server 127.0.0.1:%%PORT_8092%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /proxy/ {
            proxy_pass http://alpha;
        }

        location /read/ { }
        location /set/  { }
    }

    # upstream "backends" to serve distinguishable replies
    server {
        listen       127.0.0.1:%%PORT_8091%%;
        server_name  alpha;
        location / { return 200 "alpha\n"; }
    }

    server {
        listen       127.0.0.1:%%PORT_8092%%;
        server_name  beta;
        location / { return 200 "beta\n"; }
    }
}
EOF

$t->write_file('init.js', <<'JS');
var locs  = nginx.http.servers[0].locations;
var proxy = locs.find(function(l) { return l.path === '/proxy/'; });
var read  = locs.find(function(l) { return l.path === '/read/'; });
var set   = locs.find(function(l) { return l.path === '/set/'; });

/* 1. Read initial proxy.pass value */
read.handler = function(req) {
    req.respond(200, {'Content-Type': 'text/plain'},
                String(proxy.proxy.pass) + '\n');
};

/* 2. Switch proxy.pass to beta, then serve via the proxy location */
set.handler = function(req) {
    proxy.proxy.pass = 'beta';
    req.respond(200, {}, 'switched\n');
};
JS

$t->try_run('no js module')->plan(5);

# 1. Initial pass reads "http://alpha"
my $r1 = http_get('/read/');
like($r1, qr/alpha/, 'initial proxy.pass contains alpha');

# 2. Request via proxy — goes to alpha backend
my $r2 = http_get('/proxy/');
like($r2, qr/alpha/, 'proxy routes to alpha initially');

# 3. Switch proxy.pass to beta
my $r3 = http_get('/set/');
like($r3, qr/200/, 'set proxy.pass returns 200');

# 4. Next proxy request — now goes to beta
my $r4 = http_get('/proxy/');
like($r4, qr/beta/, 'proxy routes to beta after switch');

# 5. Pass value reflects the change
my $r5 = http_get('/read/');
like($r5, qr/beta/, 'proxy.pass now reads beta');

$t->stop();
