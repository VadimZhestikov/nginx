#!/usr/bin/perl

# Tests for NginxStreamServer COM properties (Stage 52G — stream srv conf)

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)->plan(7);

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
        return  "stream-ok\n";

        tcp_nodelay           off;
        preread_buffer_size   8192;
        preread_timeout       15s;
        resolver_timeout      10s;
        proxy_protocol_timeout 5s;
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
// Stage 52G stream COM: NginxStreamServer properties

var strmSrv = nginx.stream.servers.length > 0 ? nginx.stream.servers[0] : null;

var props = {};
if (strmSrv) {
    props.serverName           = strmSrv.serverName;
    props.tcpNodelay           = String(strmSrv.tcpNodelay);
    props.prereadBufferSize    = String(strmSrv.prereadBufferSize);
    props.prereadTimeout       = String(strmSrv.prereadTimeout);
    props.resolverTimeout      = String(strmSrv.resolverTimeout);
    props.proxyProtocolTimeout = String(strmSrv.proxyProtocolTimeout);
    props.isSrv = 'yes';
}

(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === "/prop/") {
            locs[i].handler = function(req) {
                var key = req.args.replace(/^key=/, '');
                req.respond(200, {}, props[key] !== undefined ? props[key] : "undefined");
            };
        }
    }
})();
JS

$t->run();

sub body { my $r = shift; $r =~ s/.*\r\n\r\n//s; $r }
sub prop { body(http_get("/prop/?key=$_[0]")) }

is(prop('isSrv'),                'yes',               'stream server is non-null');
is(prop('serverName'),           '',                  'serverName is empty string');
is(prop('tcpNodelay'),           'false',             'tcpNodelay off → false');
is(prop('prereadBufferSize'),    '8192',              'prereadBufferSize 8192');
is(prop('prereadTimeout'),       '15000',             'prereadTimeout 15s → 15000ms');
is(prop('resolverTimeout'),      '10000',             'resolverTimeout 10s → 10000ms');
is(prop('proxyProtocolTimeout'), '5000',              'proxyProtocolTimeout 5s → 5000ms');
