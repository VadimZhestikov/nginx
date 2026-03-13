#!/usr/bin/perl

# Stage 10: backend-module buffer setters + fastcgi.keepConn/index
#
#   fastcgi.keepConn     boolean
#   fastcgi.index        string
#   scgi.bufferSize      bytes
#   uwsgi.bufferSize     bytes
#   memcached.bufferSize bytes
#   memcached.gzipFlag   number
#
# Tests:
#   1.  initial fastcgi.keepConn=false
#   2.  set fastcgi.keepConn=true
#   3.  set fastcgi.index="index.php"
#   4.  set scgi.bufferSize=8192
#   5.  set uwsgi.bufferSize=8192
#   6.  set memcached.bufferSize=8192
#   7.  set memcached.gzipFlag=8388608
#   8.  persistence: fastcgi.keepConn still true after second request

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http fastcgi scgi uwsgi memcached/)
    ->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /fcgi/ {
            fastcgi_pass 127.0.0.1:9000;
        }

        location /scgi/ {
            scgi_pass 127.0.0.1:9001;
        }

        location /uwsgi/ {
            uwsgi_pass 127.0.0.1:9002;
        }

        location /memc/ {
            set $memcached_key $uri;
            memcached_pass 127.0.0.1:11211;
        }

        location /read/  { }
        location /set/   { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// /read/ — snapshot fields
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        fcgiKeepConn:   loc('/fcgi/').fastcgi.keepConn,
        fcgiIndex:      loc('/fcgi/').fastcgi.index,
        scgiBuf:        loc('/scgi/').scgi.bufferSize,
        uwsgiBuf:       loc('/uwsgi/').uwsgi.bufferSize,
        memcBuf:        loc('/memc/').memcached.bufferSize,
        memcGzip:       loc('/memc/').memcached.gzipFlag,
    }));
};

// /set/ — write all fields
loc('/set/').handler = r => {
    loc('/fcgi/').fastcgi.keepConn         = true;
    loc('/fcgi/').fastcgi.index            = 'index.php';
    loc('/scgi/').scgi.bufferSize          = 8192;
    loc('/uwsgi/').uwsgi.bufferSize        = 8192;
    loc('/memc/').memcached.bufferSize     = 8192;
    loc('/memc/').memcached.gzipFlag       = 8388608;
    r.respond(200, {}, 'ok');
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"fcgiKeepConn":false/, 'initial fastcgi.keepConn is false');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all backend setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"fcgiKeepConn":true/,       'fastcgi.keepConn set to true');
like($r1, qr/"fcgiIndex":"index\.php"/,  'fastcgi.index set to index.php');
like($r1, qr/"scgiBuf":8192/,            'scgi.bufferSize set to 8192');
like($r1, qr/"uwsgiBuf":8192/,           'uwsgi.bufferSize set to 8192');
like($r1, qr/"memcBuf":8192/,            'memcached.bufferSize set to 8192');
like($r1, qr/"memcGzip":8388608/,        'memcached.gzipFlag set to 8388608');

$t->stop();
