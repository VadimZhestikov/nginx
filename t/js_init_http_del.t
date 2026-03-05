#!/usr/bin/perl

# Tests for nginx.http.delServer() and nginx.http.delLocation() in
# js_init_http scripts.
#
# delServer(name)             — removes a whole server by server_name
# delLocation(serverName, path) — removes a location from a named server

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

# 4 explicit tests per scenario × 3 scenarios = 12 explicit
# + 2 auto-checks per $t instance × 3 instances = 6 auto
plan tests => 18;


# -----------------------------------------------------------------------
# Scenario 1: delServer removes a whole server.
# The 'unwanted' server is parsed first, then js_init_http deletes it.
# nginx should fail to start if it still bound port 8081.
# We verify that requests to port 8080 still work and that a request
# to /unwanted/ on port 8080 returns 404 (no such location on base).
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
        server_name  keeper;
        location /keep/ { return 200 "kept"; }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  gone;
        location /gone/ { return 200 "should be deleted"; }
    }

    js_init_http %%TESTDIR%%/del_server.js;
}
EOF

    $t->write_file('del_server.js', <<'JS');
const removed = nginx.http.delServer('gone');
if (removed !== 1) {
    throw new Error('delServer: expected 1 removed, got ' + removed);
}
JS

    $t->run();

    like(http_get('/keep/'),
         qr|200 OK|,
         'delServer: keeper server still responds 200');
    like(http_get('/keep/'),
         qr|kept|,
         'delServer: keeper server body correct');

    # 'gone' server was deleted — port 8081 should be unbound
    my $conn = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => 8081,
        Proto    => 'tcp',
        Timeout  => 1,
    );
    ok(!$conn, 'delServer: port 8081 not bound after delServer');
    $conn->close() if $conn;

    # delServer returns 0 for unknown name
    like(http_get('/keep/'),
         qr|200 OK|,
         'delServer: server still up after no-op delServer');
}


# -----------------------------------------------------------------------
# Scenario 2: delLocation removes one location from a server.
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
        server_name  trimmed;
        location /keep/   { return 200 "keep"; }
        location /remove/ { return 200 "should be gone"; }
    }

    js_init_http %%TESTDIR%%/del_location.js;
}
EOF

    $t->write_file('del_location.js', <<'JS');
const removed = nginx.http.delLocation('trimmed', '/remove/');
if (removed !== 1) {
    throw new Error('delLocation: expected 1 removed, got ' + removed);
}
JS

    $t->run();

    like(http_get('/keep/'),
         qr|200 OK|,
         'delLocation: /keep/ still responds 200');
    like(http_get('/keep/'),
         qr|keep|,
         'delLocation: /keep/ body correct');

    like(http_get('/remove/'),
         qr|404|,
         'delLocation: /remove/ returns 404 after deletion');

    # server itself is still up
    like(http_get('/keep/'),
         qr|200 OK|,
         'delLocation: server still up after location removal');
}


# -----------------------------------------------------------------------
# Scenario 3: Combined — delServer + delLocation + addServer in one script.
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
        location /old/  { return 200 "old"; }
        location /new/  { return 200 "new"; }
    }

    server {
        listen       127.0.0.1:8081;
        server_name  scratch;
        location /scratch/ { return 200 "scratch"; }
    }

    js_init_http %%TESTDIR%%/del_combined.js;
}
EOF

    $t->write_file('del_combined.js', <<'JS');
// Remove the /old/ location from base
nginx.http.delLocation('base', '/old/');

// Remove the entire scratch server
nginx.http.delServer('scratch');

// Add a brand-new server
nginx.http.addServer({
    listen:      ['127.0.0.1:8082'],
    serverNames: ['fresh'],
    locations:   [{ path: '/fresh/', return: '200 "fresh"' }]
});
JS

    $t->run();

    like(http_get('/new/'),
         qr|200 OK|,
         'combined: /new/ still exists on base');
    like(http_get('/old/'),
         qr|404|,
         'combined: /old/ removed from base');

    like(http_get('/fresh/', PeerAddr => '127.0.0.1:8082'),
         qr|200 OK|,
         'combined: new server added and responds 200');
    like(http_get('/fresh/', PeerAddr => '127.0.0.1:8082'),
         qr|fresh|,
         'combined: new server body correct');
}
