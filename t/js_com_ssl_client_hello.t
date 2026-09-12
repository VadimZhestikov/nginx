#!/usr/bin/perl

# server.ssl.onClientHello(fn) — the ClientHello parser, on malformed input.
#
# This is the only place in src/js that parses bytes from an UNAUTHENTICATED
# REMOTE PEER.  Everything else fuzzed in this hardening took its input from
# host JS — the operator's own scripts.  Here the input arrives from anyone who
# can open a TCP connection, before the handshake completes, and
# ngx_js_build_client_hello() walks the server_name and ALPN extension bodies
# with hand-written length arithmetic.  It sat at 0% coverage.
#
# The residual surface is narrower than "raw bytes": OpenSSL parses the record
# and the outer extension framing first, and only hands over an extension BODY.
# So what is worth sending is an extension whose OUTER framing is valid and
# whose INNER length fields lie — which no TLS library will ever emit, hence
# t/lib/ClientHello.pm building the records by hand.
#
# WORK VERIFICATION IS THE POINT HERE.  If OpenSSL rejects a malformed hello
# before the callback runs, this file proves nothing while looking green — the
# exact vacuous-pass shape this hardening kept running into.  So the JS hook
# counts every invocation and the test asserts the count, rather than asserting
# only that nothing crashed.
#
# The oracle is ASAN, not the assertions:
#
#     bash t/run_sanitizers.sh 'js_com_ssl_client_hello.t'
#
# A bounds error in this parser is a memory error, and the broadcast misaligned
# load showed that a sanitizer only sanitizes what you actually execute.  The
# assertions here establish that the code RAN and stayed sane; the sanitizer run
# is what establishes it is safe.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use ClientHello;
use IO::Socket::INET;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl/)->has_daemon('openssl');

my $d = $t->testdir();
system("openssl req -x509 -new -days 1 -nodes -subj /CN=ch "
     . "-newkey rsa:2048 -out $d/server.crt -keyout $d/server.key "
     . "2>$d/openssl.out") == 0
    or die "openssl req failed";

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/host.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8443 ssl;
        server_name  ch;
        ssl_certificate     %%TESTDIR%%/server.crt;
        ssl_certificate_key %%TESTDIR%%/server.key;
        location / { return 200 "tls"; }
    }

    # the report comes back over plain HTTP
    server { listen 127.0.0.1:8080; location /r { } }
}
EOF

$t->write_file('host.js', <<'JS');
var CALLS = 0;
var ERR = null;
var LAST = null;
var THREW = 0;

try {
    var ss = nginx.http.servers, i, sslsrv = null;
    for (i = 0; i < ss.length; i++) { if (ss[i].ssl) { sslsrv = ss[i]; } }
    if (!sslsrv) { ERR = 'no ssl server in the COM tree'; }
    else {
        sslsrv.ssl.onClientHello(function (ch, ctx) {
            CALLS++;
            try {
                /* touch every field the parser produces, so a bad pointer or
                 * length is dereferenced rather than merely constructed */
                var o = { sni: (ch.sni === undefined) ? null : String(ch.sni),
                          nalpn: 0, alpnLen: 0, nexts: 0, nciph: 0,
                          ver: ch.version };
                if (ch.alpn) {
                    o.nalpn = ch.alpn.length;
                    for (var a = 0; a < ch.alpn.length; a++) {
                        o.alpnLen += String(ch.alpn[a]).length;
                    }
                }
                if (ch.extensions)   { o.nexts = ch.extensions.length; }
                if (ch.cipherSuites) { o.nciph = ch.cipherSuites.length; }
                if (o.sni !== null)  { o.sniLen = o.sni.length; }
                LAST = o;
            } catch (e) { THREW++; }
            return true;     /* never abort: we want the handshake path too */
        });
    }
} catch (e) { ERR = String(e && e.message); }

var ss = nginx.http.servers;
for (var i = 0; i < ss.length; i++) {
    var L = ss[i].locations;
    for (var j = 0; j < L.length; j++) {
        if (L[j].path !== '/r') { continue; }
        L[j].handler = function (req) {
            req.respond(200, { 'content-type': 'application/json' },
                        JSON.stringify({ err: ERR, calls: CALLS,
                                         threw: THREW, last: LAST }));
        };
    }
}
JS

$t->try_run('no js module')->plan(9);

my $sslport = port(8443);

sub report {
    my $r = http_get('/r', socket => IO::Socket::INET->new(
        Proto => 'tcp', PeerAddr => '127.0.0.1:' . port(8080), Timeout => 5));
    return {} unless defined $r;
    my ($b) = $r =~ /\r\n\r\n(.*)/s;
    return {} unless defined $b;
    my $j = eval { decode_json($b) };
    return $j || {};
}

sub u8  { pack('C', $_[0]) }
sub u16 { pack('n', $_[0]) }

# --- the corpus: extension BODIES whose inner lengths lie -------------------
my @sni_bodies = (
    ['empty',          ''],
    ['one_byte',       "\x00"],
    ['exactly_five',   u16(3) . u8(0) . u16(0)],            # nlen 0, len 5
    ['nlen_at_bound',  u16(6) . u8(0) . u16(3) . 'abc'],    # nlen == len-5
    ['nlen_one_over',  u16(6) . u8(0) . u16(4) . 'abc'],    # one past the end
    ['nlen_max',       u16(6) . u8(0) . u16(0xFFFF) . 'abc'],
    ['type_not_host',  u16(6) . u8(9) . u16(3) . 'abc'],
    ['listlen_lies',   u16(0xFFFF) . u8(0) . u16(3) . 'abc'],
    ['truncated_6',    u16(3) . u8(0) . u16(0) . "\x00"],
    ['nlen_zero_tail', u16(9) . u8(0) . u16(0) . 'tail!'],
    ['big_name',       u16(0x0403) . u8(0) . u16(1024) . ('N' x 1024)],
);

my @alpn_bodies = (
    ['empty',         ''],
    ['two_bytes',     u16(0)],
    ['len_zero',      u16(1) . u8(0)],
    ['many_zero',     u16(8) . (u8(0) x 8)],
    ['pl_overruns',   u16(4) . u8(0xFF) . 'abc'],
    ['pl_at_bound',   u16(4) . u8(3) . 'abc'],
    ['pl_one_over',   u16(4) . u8(4) . 'abc'],
    ['listlen_lies',  u16(0xFFFF) . u8(2) . 'ab'],
    ['trailing_junk', u16(3) . u8(2) . 'ab' . "\xFF\xFF"],
    ['max_proto',     u16(0x0100) . u8(0xFF) . ('P' x 255)],
);

# --- baseline: a well-formed hello, to prove the harness reaches the code ---
my $before = report();
ok(!$before->{err}, 'the onClientHello hook registered')
    or diag 'host.js error: ' . ($before->{err} // 'unknown');

ClientHello::send_hello($sslport,
    ClientHello::build(sni => 'example.test', alpn => ['h2', 'http/1.1']));
select undef, undef, undef, 0.2;

my $base = report();
cmp_ok($base->{calls} || 0, '>', 0, 'a well-formed ClientHello reaches the hook')
    or diag 'nothing below means anything if the hook never runs';
is(($base->{last} || {})->{sni}, 'example.test',
   'and its SNI is parsed faithfully');
is(($base->{last} || {})->{nalpn}, 2, 'and both ALPN protocols are parsed');

# --- the malformed corpus --------------------------------------------------
my $sent = 0;
for my $c (@sni_bodies)  { $sent += ClientHello::send_hello($sslport,
                              ClientHello::build(sni_raw => $c->[1])); }
for my $c (@alpn_bodies) { $sent += ClientHello::send_hello($sslport,
                              ClientHello::build(alpn_raw => $c->[1])); }
# and both at once, every pairing of the nastiest few
for my $s (@sni_bodies[2 .. 5]) {
    for my $a (@alpn_bodies[4 .. 7]) {
        $sent += ClientHello::send_hello($sslport,
            ClientHello::build(sni_raw => $s->[1], alpn_raw => $a->[1]));
    }
}
select undef, undef, undef, 0.4;

my $after = report();
my $reached = ($after->{calls} || 0) - ($base->{calls} || 0);

diag sprintf('sent %d malformed hellos; the hook ran %d times; %d threw inside',
             $sent, $reached, $after->{threw} || 0);

cmp_ok($sent, '>=', 37, 'the whole corpus was sent');

# THE work-verification assertion.  If OpenSSL rejects these before the
# callback, this file is vacuous and must say so rather than look green.
cmp_ok($reached, '>=', 20,
       'the malformed hellos actually reached the parser')
    or diag 'OpenSSL filtered them out; this file proves nothing as written';

is($after->{threw} || 0, 0,
   'reading every parsed field threw nothing inside the hook');

# liveness: the worker is still there and the TLS server still works
my $final = report();
cmp_ok($final->{calls} || 0, '>=', $reached, 'the worker survived the corpus');

ClientHello::send_hello($sslport,
    ClientHello::build(sni => 'again.test', alpn => ['h2']));
select undef, undef, undef, 0.2;
my $post = report();
is(($post->{last} || {})->{sni}, 'again.test',
   'and still parses a well-formed hello correctly afterwards');
