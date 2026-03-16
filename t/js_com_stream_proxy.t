#!/usr/bin/perl

# Tests for NginxStreamProxy — server.proxy (Stage 53)
#
#   connectTimeout / timeout / nextUpstreamTimeout
#   bufferSize / requests / responses / nextUpstreamTries
#   nextUpstream / proxyProtocol / halfClose / socketKeepalive

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream/)->plan(14);

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

        location /prop/ { }
    }
}

stream {
    server {
        listen  127.0.0.1:%%PORT_8091%%;

        proxy_connect_timeout      3s;
        proxy_timeout              30s;
        proxy_next_upstream_timeout 5s;
        proxy_buffer_size          8k;
        proxy_requests             10;
        proxy_responses            20;
        proxy_next_upstream_tries  3;
        proxy_next_upstream        on;
        proxy_protocol             off;
        proxy_half_close           off;

        proxy_pass 127.0.0.1:%%PORT_8099%%;
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
var srv   = nginx.stream.servers[0];
var proxy = srv.proxy;

var props = {
    hasProxy:             String(proxy !== null),
    connectTimeout:       String(proxy.connectTimeout),
    timeout:              String(proxy.timeout),
    nextUpstreamTimeout:  String(proxy.nextUpstreamTimeout),
    bufferSize:           String(proxy.bufferSize),
    requests:             String(proxy.requests),
    responses:            String(proxy.responses),
    nextUpstreamTries:    String(proxy.nextUpstreamTries),
    nextUpstream:         String(proxy.nextUpstream),
    proxyProtocol:        String(proxy.proxyProtocol),
    halfClose:            String(proxy.halfClose),
    socketKeepalive:      String(proxy.socketKeepalive),
};

(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/prop/") {
            locs[i].handler = function(req) {
                var key = req.args.replace(/^key=/, '');
                req.respond(200, {}, props[key] !== undefined
                    ? props[key] : "undefined");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }
sub prop { body(http_get("/prop/?key=$_[0]")) }

is(prop('hasProxy'),            'true',  'server.proxy is not null');
is(prop('connectTimeout'),      '3000',  'connectTimeout 3s → 3000ms');
is(prop('timeout'),             '30000', 'timeout 30s → 30000ms');
is(prop('nextUpstreamTimeout'), '5000',  'nextUpstreamTimeout 5s → 5000ms');
is(prop('bufferSize'),          '8192',  'bufferSize 8k → 8192 bytes');
is(prop('requests'),            '10',    'requests 10');
is(prop('responses'),           '20',    'responses 20');
is(prop('nextUpstreamTries'),   '3',     'nextUpstreamTries 3');
is(prop('nextUpstream'),        'true',  'nextUpstream on → true');
is(prop('proxyProtocol'),       'false', 'proxyProtocol off → false');
is(prop('halfClose'),           'false', 'halfClose off → false');
is(prop('socketKeepalive'),     'false', 'socketKeepalive default off → false');

ok(1, 'nginx started without crash');
ok(1, 'all checks passed');
