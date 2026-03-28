#!/usr/bin/perl

# Tests for js_init_http directive — JS structural config hook.
#
# js_init_http fires during the http{} block parse, giving JS:
#   config.write(text)        — inject nginx directives mid-parse
#   nginx.http.servers[]      — read-only view of servers parsed so far
#
# New servers added via config.write() go through the full nginx
# merge/init_locations/optimize_servers pipeline because they are
# added during parse, not after.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

# 4 explicit tests per scenario × 2 scenarios = 8 explicit
# + 2 auto-checks per $t instance × 2 instances = 4 auto
plan tests => 12;


# -----------------------------------------------------------------------
# Scenario 1: js_init_http adds a server unconditionally via config.write.
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  base;
        location /base/ { return 200 "base server"; }
    }

    js_init_http %%TESTDIR%%/add_server.js;
}
EOF

    $t->write_file_expand('add_server.js', <<'JS');
config.write(`
    server {
        listen       127.0.0.1:%%PORT_8081%%;
        server_name  added;
        location /added/ {
            return 200 "added by js_init_http";
        }
    }
`);
JS

    $t->run();

    # Base server still works
    like(http_get('/base/'),
         qr|200 OK|,
         'base server responds 200');
    like(http_get('/base/'),
         qr|base server|,
         'base server body correct');

    # Added server works
    like(http_get('/added/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|200 OK|,
         'js_init_http added server responds 200');
    like(http_get('/added/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|added by js_init_http|,
         'js_init_http added server body correct');
}


# -----------------------------------------------------------------------
# Scenario 2: JS reads nginx.http.servers[] and conditionally adds a
# server only when a specific server_name is not already present.
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  existing;
        location /existing/ { return 200 "existing"; }
    }

    js_init_http %%TESTDIR%%/conditional.js;
}
EOF

    # conditional.js: only add "new-server" if it does not already exist.
    # Also verifies that the existing server IS visible in servers[].
    $t->write_file_expand('conditional.js', <<'JS');
const names = nginx.http.servers.map(s => s.name);

if (!names.includes('existing')) {
    throw new Error('existing server not found in nginx.http.servers');
}

if (!names.includes('new-server')) {
    config.write(`
        server {
            listen       127.0.0.1:%%PORT_8081%%;
            server_name  new-server;
            location /new/ { return 200 "conditionally added"; }
        }
    `);
}
JS

    $t->run();

    like(http_get('/existing/'),
         qr|200 OK|,
         'conditional: existing server responds 200');
    like(http_get('/existing/'),
         qr|existing|,
         'conditional: existing server body correct');

    like(http_get('/new/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|200 OK|,
         'conditional: added server responds 200');
    like(http_get('/new/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|conditionally added|,
         'conditional: added server body correct');
}
