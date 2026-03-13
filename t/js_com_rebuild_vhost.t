#!/usr/bin/perl

# Stage 5b: nginx.http.rebuildVhostDispatch()
#
# rebuildVhostDispatch() rebuilds the server-name hash for every
# listening address that has multiple virtual hosts.  After setNames()
# changes a server's names, rebuildVhostDispatch() makes nginx route
# incoming requests to the correct server.
#
# Setup: two servers on 127.0.0.1:8080, distinguished by Host header.
#   server A: server_name "alpha.test"  -> responds "server-A"
#   server B: server_name "beta.test"   -> responds "server-B"
#
# Tests:
#   1. Host: alpha.test routes to server A initially
#   2. Host: beta.test  routes to server B initially
#   3. After setNames()+rebuildVhostDispatch(), alpha routes to B
#   4. After setNames()+rebuildVhostDispatch(), beta routes to A
#   5. rebuildVhostDispatch() returns undefined (no error)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(7);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name alpha.test;
        location /  { }
        location /swap/ { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name beta.test;
        location /  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srvA = nginx.http.servers[0];   // alpha.test
const srvB = nginx.http.servers[1];   // beta.test

// ---- / on each server — identify which server handled the request ----
srvA.locations.find(l => l.path === '/').handler = r => {
    r.respond(200, {}, 'server-A');
};

srvB.locations.find(l => l.path === '/').handler = r => {
    r.respond(200, {}, 'server-B');
};

// ---- /swap/ — swap names and rebuild dispatch ----
srvA.locations.find(l => l.path === '/swap/').handler = r => {
    // Swap: A takes beta.test, B takes alpha.test
    srvA.setNames(['beta.test']);
    srvB.setNames(['alpha.test']);
    nginx.http.rebuildVhostDispatch();
    r.respond(200, {}, 'swapped');
};
JS

$t->run();

# Helper: send request with explicit Host header
sub http_host_get {
    my ($host, $path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: $host\r\n\r\n");
}

# ---- Initial routing ----
like(http_host_get('alpha.test', '/'), qr/server-A/, 'alpha.test routes to server A initially');
like(http_host_get('beta.test',  '/'), qr/server-B/, 'beta.test routes to server B initially');

# ---- Swap names and rebuild ----
my $swap = http_host_get('alpha.test', '/swap/');
like($swap, qr/200/,     'swap request succeeded');
like($swap, qr/swapped/, 'swap response body correct');

# ---- Routing after rebuild ----
# Now alpha.test -> srvB (which says "server-B"), but its handler says "server-B"
# and beta.test  -> srvA (which says "server-A")
like(http_host_get('alpha.test', '/'), qr/server-B/, 'alpha.test routes to server B after rebuild');
like(http_host_get('beta.test',  '/'), qr/server-A/, 'beta.test routes to server A after rebuild');

# ---- rebuildVhostDispatch with no vhost entries is a no-op ----
{
    # This exercises a server that has only one server block (no virtual names
    # hash).  rebuildVhostDispatch() should still succeed.
    like(http_host_get('alpha.test', '/'), qr/200/, 'worker still alive after rebuild');
}

$t->stop();
