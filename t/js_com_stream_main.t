#!/usr/bin/perl

# Tests for nginx.stream main-conf properties (Stage 53)
#
#   nginx.stream.serverNamesHashMaxSize
#   nginx.stream.serverNamesHashBucketSize
#   nginx.stream.variablesHashMaxSize
#   nginx.stream.variablesHashBucketSize

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream stream_return/)->plan(6);

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
    server_names_hash_max_size    512;
    server_names_hash_bucket_size 64;
    variables_hash_max_size       512;
    variables_hash_bucket_size    64;

    server {
        listen  127.0.0.1:%%PORT_8091%%;
        return  "ok\n";
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
var sm = nginx.stream;

var props = {
    serverNamesHashMaxSize:    String(sm.serverNamesHashMaxSize),
    serverNamesHashBucketSize: String(sm.serverNamesHashBucketSize),
    variablesHashMaxSize:      String(sm.variablesHashMaxSize),
    variablesHashBucketSize:   String(sm.variablesHashBucketSize),
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

is(prop('serverNamesHashMaxSize'),    '512', 'serverNamesHashMaxSize 512');
is(prop('serverNamesHashBucketSize'), '64',  'serverNamesHashBucketSize 64');
is(prop('variablesHashMaxSize'),      '512', 'variablesHashMaxSize 512');
is(prop('variablesHashBucketSize'),   '64',  'variablesHashBucketSize 64');

ok(1, 'nginx started without crash');
ok(1, 'all checks passed');
