#!/usr/bin/perl

# Tests for nginx.http.addServer() / srv.addLocation() builder API
# in js_init_http scripts.
#
# The builder is a fluent alternative to config.write() that constructs
# server{} blocks programmatically.  Servers are flushed via a temporary
# config file after JS_Eval returns, so they receive the full nginx
# merge/init_locations/optimize_servers treatment.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

# 4 tests per scenario × 3 scenarios = 12 explicit
# + 2 auto-checks per $t instance × 3 instances = 6 auto
plan tests => 18;


# -----------------------------------------------------------------------
# Scenario 1: addServer with listen + serverNames, locations via
#             srv.addLocation() chaining.
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
        location /base/ { return 200 "base"; }
    }

    js_init_http %%TESTDIR%%/builder_chain.js;
}
EOF

    $t->write_file_expand('builder_chain.js', <<'JS');
const srv = nginx.http.addServer({
    listen:      ['127.0.0.1:%%PORT_8081%%'],
    serverNames: ['chain-srv']
});

srv.addLocation('/hello/', { return: '200 "hello from builder"' })
   .addLocation('/world/', { return: '200 "world from builder"' });
JS

    $t->run();

    like(http_get('/base/'),
         qr|200 OK|,
         'builder: base server still responds 200');
    like(http_get('/base/'),
         qr|base|,
         'builder: base server body correct');

    like(http_get('/hello/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|200 OK|,
         'builder: chained addLocation /hello/ responds 200');
    like(http_get('/hello/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|hello from builder|,
         'builder: chained addLocation /hello/ body correct');
}


# -----------------------------------------------------------------------
# Scenario 2: addServer with inline locations[] in the opts object.
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
        server_name  base2;
        location /base/ { return 200 "base2"; }
    }

    js_init_http %%TESTDIR%%/builder_inline.js;
}
EOF

    $t->write_file_expand('builder_inline.js', <<'JS');
nginx.http.addServer({
    listen:      ['127.0.0.1:%%PORT_8081%%'],
    serverNames: ['inline-srv'],
    locations: [
        { path: '/a/', return: '200 "inline-a"' },
        { path: '/b/', return: '200 "inline-b"' }
    ]
});
JS

    $t->run();

    like(http_get('/base/'),
         qr|200 OK|,
         'inline: base server still responds 200');
    like(http_get('/base/'),
         qr|base2|,
         'inline: base server body correct');

    like(http_get('/a/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|200 OK|,
         'inline: /a/ responds 200');
    like(http_get('/a/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|inline-a|,
         'inline: /a/ body correct');
}


# -----------------------------------------------------------------------
# Scenario 3: Multiple addServer calls in one script.
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
        server_name  base3;
        location /base/ { return 200 "base3"; }
    }

    js_init_http %%TESTDIR%%/builder_multi.js;
}
EOF

    $t->write_file_expand('builder_multi.js', <<'JS');
nginx.http.addServer({
    listen:      ['127.0.0.1:%%PORT_8081%%'],
    serverNames: ['first'],
    locations: [{ path: '/first/', return: '200 "first server"' }]
});

nginx.http.addServer({
    listen:      ['127.0.0.1:%%PORT_8082%%'],
    serverNames: ['second'],
    locations: [{ path: '/second/', return: '200 "second server"' }]
});
JS

    $t->run();

    like(http_get('/first/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|200 OK|,
         'multi: first server responds 200');
    like(http_get('/first/', PeerAddr => '127.0.0.1', PeerPort => port(8081)),
         qr|first server|,
         'multi: first server body correct');

    like(http_get('/second/', PeerAddr => '127.0.0.1', PeerPort => port(8082)),
         qr|200 OK|,
         'multi: second server responds 200');
    like(http_get('/second/', PeerAddr => '127.0.0.1', PeerPort => port(8082)),
         qr|second server|,
         'multi: second server body correct');
}
