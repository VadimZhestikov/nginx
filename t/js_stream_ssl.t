#!/usr/bin/perl

# Tests for nginx.stream.servers[i].ssl — NginxStreamSSL (Stage D).
#
# Covers:
#   srv.ssl                  — NginxStreamSSL object (or null for plain server)
#   ssl.protocols            — string[] getter
#   ssl.ciphers              — string getter
#   ssl.certificate          — string[] getter
#   ssl.certificateKey       — string[] getter
#   ssl.sessionTimeout       — number getter + setter
#   ssl.sessionTickets       — bool getter + setter
#   ssl.preferServerCiphers  — bool getter + setter
#   ssl.verifyDepth          — number getter + setter
#   ssl.handshakeTimeout     — number getter + setter

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream_ssl/)->has_daemon('openssl');

my $d = $t->testdir();

system('openssl req -x509 -new -days 1 -nodes '
     . "-subj '/CN=localhost' "
     . "-out $d/server.crt -keyout $d/server.key "
     . "2>$d/openssl.out") == 0
    or die "openssl req failed";

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
        location / { }
    }
}

stream {
    server {
        listen      127.0.0.1:%%PORT_8092%% ssl;

        ssl_certificate     %%TESTDIR%%/server.crt;
        ssl_certificate_key %%TESTDIR%%/server.key;

        ssl_protocols             TLSv1.2 TLSv1.3;
        ssl_ciphers               HIGH:!aNULL:!MD5;
        ssl_prefer_server_ciphers on;
        ssl_session_timeout       30m;
        ssl_session_tickets       off;
        ssl_verify_client         off;
        ssl_verify_depth          3;
        ssl_handshake_timeout     30s;

        proxy_pass  127.0.0.1:%%PORT_8091%%;
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.stream.servers[0];
const ssl = srv.ssl;

/* ---- ssl object --------------------------------------------------- */
check("ssl_obj",   typeof ssl === "object" && ssl !== null, typeof ssl);

/* ---- protocols ---------------------------------------------------- */
const p = ssl.protocols;
check("proto_arr",    Array.isArray(p),                typeof p);
check("proto_12",     p.indexOf("TLSv1.2") !== -1,     p);
check("proto_13",     p.indexOf("TLSv1.3") !== -1,     p);
check("proto_no_10",  p.indexOf("TLSv1") === -1,       p);

/* ---- ciphers ------------------------------------------------------ */
check("ciphers_str",  typeof ssl.ciphers === "string" && ssl.ciphers.length > 0,
      ssl.ciphers);
check("ciphers_hi",   ssl.ciphers.indexOf("HIGH") !== -1, ssl.ciphers);

/* ---- certificate / certificateKey --------------------------------- */
const certs = ssl.certificate;
check("cert_arr",   Array.isArray(certs),            typeof certs);
check("cert_1",     certs.length === 1,              certs.length);
check("cert_path",  certs[0].indexOf(".crt") !== -1, certs[0]);

const keys = ssl.certificateKey;
check("key_arr",    Array.isArray(keys),             typeof keys);
check("key_1",      keys.length === 1,               keys.length);
check("key_path",   keys[0].indexOf(".key") !== -1,  keys[0]);

/* ---- initial getter values ---------------------------------------- */
check("sess_timeout_init",    ssl.sessionTimeout    === 1800,  ssl.sessionTimeout);
check("sess_tickets_init",    ssl.sessionTickets    === false, ssl.sessionTickets);
check("pref_ciphers_init",    ssl.preferServerCiphers === true, ssl.preferServerCiphers);
check("verify_depth_init",    ssl.verifyDepth       === 3,     ssl.verifyDepth);
check("handshake_timeout_init", ssl.handshakeTimeout === 30000, ssl.handshakeTimeout);

/* ---- setters ------------------------------------------------------- */
ssl.sessionTimeout = 600;
check("sess_timeout_set",  ssl.sessionTimeout === 600, ssl.sessionTimeout);

ssl.sessionTickets = true;
check("sess_tickets_set",  ssl.sessionTickets === true, ssl.sessionTickets);

ssl.preferServerCiphers = false;
check("pref_ciphers_set",  ssl.preferServerCiphers === false, ssl.preferServerCiphers);

ssl.verifyDepth = 5;
check("verify_depth_set",  ssl.verifyDepth === 5, ssl.verifyDepth);

ssl.handshakeTimeout = 60000;
check("handshake_timeout_set", ssl.handshakeTimeout === 60000, ssl.handshakeTimeout);
JS

$t->try_run('no js module or stream_ssl module')->plan(23);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS ssl_obj/,              'srv.ssl is object');
like($log, qr/JSTEST PASS proto_arr/,            'ssl.protocols is array');
like($log, qr/JSTEST PASS proto_12/,             'ssl.protocols includes TLSv1.2');
like($log, qr/JSTEST PASS proto_13/,             'ssl.protocols includes TLSv1.3');
like($log, qr/JSTEST PASS proto_no_10/,          'ssl.protocols excludes TLSv1');
like($log, qr/JSTEST PASS ciphers_str/,          'ssl.ciphers is non-empty string');
like($log, qr/JSTEST PASS ciphers_hi/,           'ssl.ciphers contains HIGH');
like($log, qr/JSTEST PASS cert_arr/,             'ssl.certificate is array');
like($log, qr/JSTEST PASS cert_1/,               'ssl.certificate has 1 entry');
like($log, qr/JSTEST PASS cert_path/,            'ssl.certificate[0] path');
like($log, qr/JSTEST PASS key_arr/,              'ssl.certificateKey is array');
like($log, qr/JSTEST PASS key_1/,               'ssl.certificateKey has 1 entry');
like($log, qr/JSTEST PASS key_path/,             'ssl.certificateKey[0] path');
like($log, qr/JSTEST PASS sess_timeout_init/,    'ssl.sessionTimeout initial == 1800');
like($log, qr/JSTEST PASS sess_tickets_init/,    'ssl.sessionTickets initial == false');
like($log, qr/JSTEST PASS pref_ciphers_init/,    'ssl.preferServerCiphers initial == true');
like($log, qr/JSTEST PASS verify_depth_init/,    'ssl.verifyDepth initial == 3');
like($log, qr/JSTEST PASS handshake_timeout_init/, 'ssl.handshakeTimeout initial == 30000');
like($log, qr/JSTEST PASS sess_timeout_set/,     'ssl.sessionTimeout setter');
like($log, qr/JSTEST PASS sess_tickets_set/,     'ssl.sessionTickets setter');
like($log, qr/JSTEST PASS pref_ciphers_set/,     'ssl.preferServerCiphers setter');
like($log, qr/JSTEST PASS verify_depth_set/,     'ssl.verifyDepth setter');
like($log, qr/JSTEST PASS handshake_timeout_set/, 'ssl.handshakeTimeout setter');
