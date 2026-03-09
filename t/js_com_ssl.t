#!/usr/bin/perl

# Tests for Stage 6 COM expansion: ngx_http_ssl_srv_conf_t fields
# exposed as properties of server.ssl (NginxSSL class).
#
# New property on NginxServer:
#   ssl   NginxSSL | null (null if no ssl_certificate directive)
#
# NginxSSL properties (all read-only):
#   protocols            string[]   active TLS version names
#   ciphers              string     OpenSSL cipher string
#   certificate          string[]   certificate file paths
#   certificateKey       string[]   certificate key file paths
#   sessionTimeout       number     session timeout (seconds)
#   sessionTickets       bool       TLS session tickets enabled
#   preferServerCiphers  bool       ssl_prefer_server_ciphers
#   verify               string     client verify mode
#   verifyDepth          number     client cert chain depth limit
#   clientCertificate    string     client CA cert path (or "")
#   trustedCertificate   string     trusted CA cert path (or "")
#   ecdhCurve            string     ECDH curve name
#   dhparam              string     DH params file path (or "")

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

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init_ssl.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    # SSL server with all explicitly-set options we test
    server {
        listen              127.0.0.1:8443 ssl;
        server_name         localhost;

        ssl_certificate     %%TESTDIR%%/server.crt;
        ssl_certificate_key %%TESTDIR%%/server.key;

        ssl_protocols             TLSv1.2 TLSv1.3;
        ssl_ciphers               HIGH:!aNULL:!MD5;
        ssl_prefer_server_ciphers on;
        ssl_session_timeout       30m;
        ssl_session_tickets       off;
        ssl_verify_client         off;
        ssl_verify_depth          3;

        location / { }
    }

    # Plain HTTP server — server.ssl must be null
    server {
        listen      127.0.0.1:8080;
        server_name plain;
        location /  { }
    }
}
EOF

$t->write_file('init_ssl.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const ssl_srv   = nginx.http.servers[0];
const plain_srv = nginx.http.servers[1];

const s = ssl_srv.ssl;

// ---- ssl object ----
check("ssl_obj",   typeof s === "object" && s !== null, typeof s);

// ---- protocols ----
const p = s.protocols;
check("proto_arr",    Array.isArray(p),                   typeof p);
check("proto_12",     p.indexOf("TLSv1.2") !== -1,        p);
check("proto_13",     p.indexOf("TLSv1.3") !== -1,        p);
check("proto_no_10",  p.indexOf("TLSv1") === -1,          p);

// ---- ciphers ----
check("ciphers_str",  typeof s.ciphers === "string" && s.ciphers.length > 0, s.ciphers);
check("ciphers_hi",   s.ciphers.indexOf("HIGH") !== -1, s.ciphers);

// ---- certificate / certificateKey ----
const certs = s.certificate;
check("cert_arr",     Array.isArray(certs),             typeof certs);
check("cert_1",       certs.length === 1,               certs.length);
check("cert_path",    certs[0].indexOf(".crt") !== -1,  certs[0]);

const keys = s.certificateKey;
check("key_arr",      Array.isArray(keys),              typeof keys);
check("key_1",        keys.length === 1,                keys.length);
check("key_path",     keys[0].indexOf(".key") !== -1,   keys[0]);

// ---- session settings ----
check("sess_timeout", s.sessionTimeout === 1800, s.sessionTimeout);
check("sess_tickets", s.sessionTickets === false, s.sessionTickets);

// ---- preferServerCiphers ----
check("pref_ciphers", s.preferServerCiphers === true, s.preferServerCiphers);

// ---- verify ----
check("verify_off",   s.verify === "off", s.verify);
check("verify_depth", s.verifyDepth === 3, s.verifyDepth);

// ---- plain server: ssl === null ----
check("plain_ssl_null", plain_srv.ssl === null, typeof plain_srv.ssl);
JS

$t->try_run('no js module')->plan(19);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ssl_obj/,        'server.ssl is object');
like($log, qr/JSTEST PASS proto_arr/,      'server.ssl.protocols is array');
like($log, qr/JSTEST PASS proto_12/,       'server.ssl.protocols includes TLSv1.2');
like($log, qr/JSTEST PASS proto_13/,       'server.ssl.protocols includes TLSv1.3');
like($log, qr/JSTEST PASS proto_no_10/,    'server.ssl.protocols excludes TLSv1');
like($log, qr/JSTEST PASS ciphers_str/,    'server.ssl.ciphers is string');
like($log, qr/JSTEST PASS ciphers_hi/,     'server.ssl.ciphers contains HIGH');
like($log, qr/JSTEST PASS cert_arr/,       'server.ssl.certificate is array');
like($log, qr/JSTEST PASS cert_1/,         'server.ssl.certificate has 1 entry');
like($log, qr/JSTEST PASS cert_path/,      'server.ssl.certificate[0] path');
like($log, qr/JSTEST PASS key_arr/,        'server.ssl.certificateKey is array');
like($log, qr/JSTEST PASS key_1/,          'server.ssl.certificateKey has 1 entry');
like($log, qr/JSTEST PASS key_path/,       'server.ssl.certificateKey[0] path');
like($log, qr/JSTEST PASS sess_timeout/,   'server.ssl.sessionTimeout == 1800');
like($log, qr/JSTEST PASS sess_tickets/,   'server.ssl.sessionTickets == false');
like($log, qr/JSTEST PASS pref_ciphers/,   'server.ssl.preferServerCiphers == true');
like($log, qr/JSTEST PASS verify_off/,     'server.ssl.verify == "off"');
like($log, qr/JSTEST PASS verify_depth/,   'server.ssl.verifyDepth == 3');
like($log, qr/JSTEST PASS plain_ssl_null/, 'plain server.ssl == null');
