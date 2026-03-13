#!/usr/bin/perl

# Stage 3: NginxSSL.setProtocols(arr)
#
# setProtocols(arr) updates the enabled TLS protocol versions on the live
# SSL_CTX and updates sscf->protocols so the COM protocols getter stays
# consistent.
#
# Tests:
#   1. protocols getter returns config-time value (TLSv1.2)
#   2. setProtocols(["TLSv1.2","TLSv1.3"]) succeeds without exception
#   3. protocols getter shows new array immediately
#   4. Persistence: a second /read/ still shows the updated protocols
#   5. setProtocols([]) — empty array succeeds (disables all versions)
#   6. protocols getter returns empty array after setProtocols([])
#   7. Error path: non-array argument throws TypeError

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

$t->plan(9);

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

        location /read/    { }
        location /set/     { }
        location /empty/   { }
        location /badarg/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const ssl_srv   = nginx.http.servers[0];
const plain_srv = nginx.http.servers[1];

function loc(path) {
    return plain_srv.locations.find(l => l.path === path);
}

// ---- /read/ — return protocols array as JSON ----
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify(ssl_srv.ssl.protocols));
};

// ---- /set/ — add TLSv1.3 alongside TLSv1.2 ----
loc('/set/').handler = r => {
    ssl_srv.ssl.setProtocols(['TLSv1.2', 'TLSv1.3']);
    r.respond(200, {}, JSON.stringify(ssl_srv.ssl.protocols));
};

// ---- /empty/ — disable all protocol versions ----
loc('/empty/').handler = r => {
    ssl_srv.ssl.setProtocols([]);
    r.respond(200, {}, JSON.stringify(ssl_srv.ssl.protocols));
};

// ---- /badarg/ — non-array argument should throw TypeError ----
loc('/badarg/').handler = r => {
    try {
        ssl_srv.ssl.setProtocols('TLSv1.2');
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial protocols from config (TLSv1.2 only) ----
my $r0 = http_get('/read/');
like($r0, qr/TLSv1\.2/, 'protocols getter returns config-time TLSv1.2');
unlike($r0, qr/TLSv1\.3/, 'TLSv1.3 not present in initial config');

# ---- setProtocols(["TLSv1.2","TLSv1.3"]) ----
my $r1 = http_get('/set/');
like($r1, qr/200/,        'setProtocols() succeeds without exception');
like($r1, qr/TLSv1\.2/,  'TLSv1.2 in updated protocols');
like($r1, qr/TLSv1\.3/,  'TLSv1.3 in updated protocols');

# ---- Persistence ----
like(http_get('/read/'), qr/TLSv1\.3/, 'updated protocols persist on next request');

# ---- setProtocols([]) — empty array ----
my $r2 = http_get('/empty/');
like($r2, qr/200/,  'setProtocols([]) succeeds');
like($r2, qr/\[\]/, 'protocols getter returns empty array after setProtocols([])');

# ---- Error path: non-array argument ----
my $r3 = http_get('/badarg/');
like($r3, qr/error:/, 'setProtocols with non-array throws TypeError');

$t->stop();
