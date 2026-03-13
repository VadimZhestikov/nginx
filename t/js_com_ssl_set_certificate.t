#!/usr/bin/perl

# Stage 4: NginxSSL.setCertificate(certPath, keyPath)
#
# setCertificate(certPath, keyPath) calls SSL_CTX_use_certificate_chain_file()
# and SSL_CTX_use_PrivateKey_file(), verifies the pair with
# SSL_CTX_check_private_key(), then updates sscf->certificates and
# sscf->certificate_keys so the COM getters reflect the new paths.
#
# Tests:
#   1. certificate getter returns initial path after config
#   2. setCertificate() succeeds without exception
#   3. certificate getter returns new path immediately
#   4. certificateKey getter returns new key path
#   5. Persistence: a second /read/ still shows the new path
#   6. Error path: mismatched cert+key pair throws
#   7. Error path: non-existent file throws

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

# Generate two independent self-signed certificates
for my $name (qw(server alt)) {
    system('openssl req -x509 -new -days 1 -nodes '
         . "-subj '/CN=$name' "
         . "-out $d/$name.crt -keyout $d/$name.key "
         . "2>$d/openssl-$name.out") == 0
        or die "openssl req failed for $name";
}

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

        location /read/     { }
        location /set/      { }
        location /mismatch/ { }
        location /missing/  { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JSEOF');
const ssl_srv   = nginx.http.servers[0];
const plain_srv = nginx.http.servers[1];

const ALT_CERT = '%%TESTDIR%%/alt.crt';
const ALT_KEY  = '%%TESTDIR%%/alt.key';
const SRV_KEY  = '%%TESTDIR%%/server.key';

function loc(path) {
    return plain_srv.locations.find(l => l.path === path);
}

// ---- /read/ — return current cert[0] and key[0] paths ----
loc('/read/').handler = r => {
    const certs = ssl_srv.ssl.certificate;
    const keys  = ssl_srv.ssl.certificateKey;
    r.respond(200, {}, JSON.stringify({
        cert: certs.length > 0 ? certs[0] : '',
        key:  keys.length  > 0 ? keys[0]  : '',
    }));
};

// ---- /set/ — swap to alt cert ----
loc('/set/').handler = r => {
    ssl_srv.ssl.setCertificate(ALT_CERT, ALT_KEY);
    const certs = ssl_srv.ssl.certificate;
    const keys  = ssl_srv.ssl.certificateKey;
    r.respond(200, {}, JSON.stringify({
        cert: certs.length > 0 ? certs[0] : '',
        key:  keys.length  > 0 ? keys[0]  : '',
    }));
};

// ---- /mismatch/ — alt cert + server key (different CN, key mismatch) ----
loc('/mismatch/').handler = r => {
    try {
        ssl_srv.ssl.setCertificate(ALT_CERT, SRV_KEY);
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};

// ---- /missing/ — non-existent cert file ----
loc('/missing/').handler = r => {
    try {
        ssl_srv.ssl.setCertificate('/no/such/file.crt', ALT_KEY);
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JSEOF

$t->run();

# ---- Initial cert path from config ----
my $r0 = http_get('/read/');
like($r0, qr/server\.crt/, 'certificate getter returns initial server.crt path');

# ---- setCertificate() swap to alt ----
my $r1 = http_get('/set/');
like($r1, qr/200/,       'setCertificate() succeeds without exception');
like($r1, qr/alt\.crt/,  'certificate getter returns new alt.crt path');
like($r1, qr/alt\.key/,  'certificateKey getter returns new alt.key path');

# ---- Persistence ----
like(http_get('/read/'), qr/alt\.crt/, 'new certificate path persists on next request');

# ---- Error path: mismatched key ----
my $r2 = http_get('/mismatch/');
like($r2, qr/error:/, 'setCertificate with mismatched key throws');
unlike($r2, qr/no-error/, 'no-error not returned for mismatched key');

# ---- Error path: missing file ----
my $r3 = http_get('/missing/');
like($r3, qr/error:/, 'setCertificate with missing file throws');
unlike($r3, qr/no-error/, 'no-error not returned for missing file');

$t->stop();
