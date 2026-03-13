#!/usr/bin/perl

# Stage 2: NginxSSL.setCiphers(str)
#
# setCiphers(str) calls SSL_CTX_set_cipher_list() on the live SSL_CTX and
# updates sscf->ciphers so the COM ciphers getter reflects the new value.
#
# Tests:
#   1. Read initial ciphers from config
#   2. Call setCiphers() from a request handler — no exception
#   3. ciphers getter returns the new string immediately
#   4. Persistence: a second /read/ request still shows the new string
#   5. Error path: setCiphers("INVALID!!!") throws

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl/)->has_daemon('openssl');

my $d = $t->testdir();

system('openssl req -x509 -new -days 1 -nodes '
     . "-subj '/CN=localhost' "
     . "-out $d/server.crt -keyout $d/server.key "
     . "2>$d/openssl.out") == 0
    or die "openssl req failed";

$t->plan(8);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen              127.0.0.1:8443 ssl;
        server_name         localhost;

        ssl_certificate     %%TESTDIR%%/server.crt;
        ssl_certificate_key %%TESTDIR%%/server.key;

        ssl_ciphers         HIGH:!aNULL:!MD5;
        ssl_protocols       TLSv1.2;

        location / { }
    }

    server {
        listen      127.0.0.1:8080;
        server_name plain;

        location /read/   { }
        location /set/    { }
        location /badset/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const ssl_srv   = nginx.http.servers[0];   // SSL server
const plain_srv = nginx.http.servers[1];   // plain HTTP server

function loc(path) {
    return plain_srv.locations.find(l => l.path === path);
}

// ---- /read/ — return current ciphers string ----
loc('/read/').handler = r => {
    r.respond(200, {}, ssl_srv.ssl.ciphers);
};

// ---- /set/ — change ciphers via setCiphers() ----
loc('/set/').handler = r => {
    ssl_srv.ssl.setCiphers('AES128-SHA:AES256-SHA');
    r.respond(200, {}, ssl_srv.ssl.ciphers);
};

// ---- /badset/ — attempt an invalid cipher list ----
loc('/badset/').handler = r => {
    try {
        ssl_srv.ssl.setCiphers('TOTALLY_INVALID_CIPHER_STRING!!!');
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial ciphers from config ----
like(http_get('/read/'), qr/HIGH/,  'ciphers getter returns config-time string');

# ---- setCiphers() from a request handler ----
my $r2 = http_get('/set/');
like($r2, qr/200/,          'setCiphers() succeeds without exception');
like($r2, qr/AES128-SHA/,   'ciphers getter updated immediately after setCiphers');
like($r2, qr/AES256-SHA/,   'both ciphers present in updated string');

# ---- Persistence ----
like(http_get('/read/'), qr/AES128-SHA/, 'new ciphers persist on subsequent request');
unlike(http_get('/read/'), qr/HIGH/,     'old cipher string is gone');

# ---- Error path: invalid cipher list ----
my $r3 = http_get('/badset/');
like($r3, qr/error:/,    'setCiphers with invalid list throws');
unlike($r3, qr/no-error/, 'no-error not returned for invalid cipher list');

$t->stop();
