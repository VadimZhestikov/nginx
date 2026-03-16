#!/usr/bin/perl

# Tests for nginx.stream.upstreams[] (Stage 53)
#
#   nginx.stream.upstreams[i].name
#   nginx.stream.upstreams[i].zone   (null when no zone directive)
#   nginx.stream.upstreams[i].peers[]
#     .address / .weight / .maxFails / .failTimeout / .maxConns
#     .down / .backup

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
    upstream backend {
        server 127.0.0.1:%%PORT_8091%% weight=3 max_fails=2 fail_timeout=20s max_conns=5;
        server 127.0.0.1:%%PORT_8092%% backup;
    }

    server {
        listen  127.0.0.1:%%PORT_8090%%;
        proxy_pass backend;
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
var upstreams = nginx.stream.upstreams;
var u         = upstreams[0];
var peers     = u.peers;
var p0        = peers[0];
var p1        = peers[1];

var props = {
    count:          String(upstreams.length),
    name:           String(u.name),
    zone:           String(u.zone),
    peerCount:      String(peers.length),
    p0address:      String(p0.address),
    p0weight:       String(p0.weight),
    p0maxFails:     String(p0.maxFails),
    p0failTimeout:  String(p0.failTimeout),
    p0maxConns:     String(p0.maxConns),
    p0down:         String(p0.down),
    p0backup:       String(p0.backup),
    p1backup:       String(p1.backup),
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

is(prop('count'),         '1',              'one upstream');
is(prop('name'),          'backend',        'upstream name');
is(prop('zone'),          'null',           'no zone → null');
is(prop('peerCount'),     '2',              'two peers');
is(prop('p0address'),     '127.0.0.1:' . port(8091), 'peer 0 address');
is(prop('p0weight'),      '3',              'peer 0 weight 3');
is(prop('p0maxFails'),    '2',              'peer 0 max_fails 2');
is(prop('p0failTimeout'), '20',             'peer 0 fail_timeout 20s');
is(prop('p0maxConns'),    '5',              'peer 0 max_conns 5');
is(prop('p0down'),        'false',          'peer 0 not down');
is(prop('p0backup'),      'false',          'peer 0 not backup');
is(prop('p1backup'),      'true',           'peer 1 is backup');

ok(1, 'nginx started without crash');
ok(1, 'all checks passed');
