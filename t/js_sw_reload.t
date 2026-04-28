#!/usr/bin/perl

# Tests for SharedWorker state across nginx reload (SIGHUP).
# Verifies that reload does not crash the master process and that the
# SharedWorker is available and functional after the reload.

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

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /echo/ { }
    }
}
EOF

$t->write_file('echo_sw.js', <<'JS');
onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(ev) {
        port.postMessage(ev.data);
    };
};
JS

$t->write_file('init.js', <<'JS');
(function() {
    var prefix = nginx.cycle.prefix;
    var echoSW = new SharedWorker(prefix + 'echo_sw.js');

    var loc = nginx.http.servers[0].locations
        .find(function(l) { return l.path === '/echo/'; });

    loc.handler = async function(req) {
        var sw = new SharedWorker(nginx.cycle.prefix + 'echo_sw.js');
        var result = await new Promise(function(resolve) {
            sw.onmessage = function(e) { resolve(e.data); };
            sw.postMessage('hello');
        });
        req.respond(200, {'content-type': 'text/plain'}, String(result));
    };
})();
JS

$t->try_run('no js module')->plan(6);

# Wait until nginx is accepting connections (up to 5s).
sub wait_ready {
    for (1 .. 50) {
        my $r = http_get('/echo/');
        return $r if defined $r && $r =~ /200 OK/;
        select undef, undef, undef, 0.1;
    }
    return undef;
}

# Verify baseline: SW works before reload
like(http_get('/echo/'), qr/200 OK/,  'pre-reload: status 200');
like(http_get('/echo/'), qr/hello/,   'pre-reload: body matches');

# Reload nginx — must not crash
$t->reload();

# Wait for new workers to come up, then verify
my $r1 = wait_ready();
like($r1, qr/200 OK/,  'post-reload 1: status 200');
like($r1, qr/hello/,   'post-reload 1: body matches');

# Second reload
$t->reload();

my $r2 = wait_ready();
like($r2, qr/200 OK/,  'post-reload 2: status 200');
like($r2, qr/hello/,   'post-reload 2: body matches');
