#!/usr/bin/perl

# server.ssl scalar setters — numeric range and type validation.
#
# `ngx_js_ssl_set` backs the four scalar TLS properties (sessionTimeout,
# sessionTickets, preferServerCiphers, verifyDepth).  The existing SSL tests
# exercise the METHODS next to it -- setCertificate, setCiphers, setProtocols --
# and the getters, so a gcov run over the whole suite found this setter at 0%:
# nothing had ever written one of those four.
#
# What was waiting there is the same defect as the upstream peer setters
# (commit 5186565a1): the value was cast, not checked.  JS_ToInt64() answers 0
# for NaN, for {} and for "abc" WITHOUT reporting an error, so
#
#     ssl.verifyDepth = {}        // a typo
#
# silently set the certificate verification depth to 0 -- a security control
# weakened, nothing raised.  Negatives were taken as well: verifyDepth is stored
# in an ngx_uint_t and then handed to SSL_CTX_set_verify_depth() as an int, and
# 2^31 wrapped to INT_MIN on the way.  sessionTimeout took -1 straight through
# to SSL_CTX_set_timeout().
#
# Both numeric properties now use nginx's own ranges for the directives that
# write the same fields, and refuse non-numbers rather than coercing them.
#
# The two BOOLEAN properties are deliberately left coercing: `ssl.sessionTickets
# = {}` is true and `= ''` is false, which is what a boolean property should do
# in JavaScript.  That is pinned below so the distinction stays a decision
# rather than an oversight.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http http_ssl/)->has_daemon('openssl');

my $d = $t->testdir();
system('openssl req -x509 -new -days 1 -nodes '
     . '-subj "/CN=sslrange" -newkey rsa:2048 '
     . "-out $d/server.crt -keyout $d/server.key "
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
        listen       127.0.0.1:8080 ssl;
        server_name  sslrange;

        ssl_certificate     %%TESTDIR%%/server.crt;
        ssl_certificate_key %%TESTDIR%%/server.key;
        ssl_session_timeout 10m;
        ssl_verify_depth    2;

        location /s { }
    }

    # plain server: the report comes back over this one
    server {
        listen       127.0.0.1:8081;
        location /r { }
    }
}
EOF

$t->write_file('host.js', <<'JS');
var OUT = { refused: [], accepted: [], wrong: [], bools: {} };

function findSsl() {
    var ss = nginx.http.servers, i;
    for (i = 0; i < ss.length; i++) {
        try { if (ss[i].ssl) { return ss[i].ssl; } } catch (e) { /* skip */ }
    }
    return null;
}

function mustRefuse(s, prop, v, tag) {
    var before = s[prop];
    try {
        s[prop] = v;
        OUT.wrong.push(prop + '=' + tag + ' accepted -> ' + String(s[prop]));
        try { s[prop] = before; } catch (e) { /* best effort */ }
    } catch (e) {
        OUT.refused.push(prop + ':' + tag);
    }
}

function mustAccept(s, prop, v) {
    try {
        s[prop] = v;
        if (s[prop] === v) { OUT.accepted.push(prop + '=' + v); }
        else { OUT.wrong.push(prop + '=' + v + ' read back ' + String(s[prop])); }
    } catch (e) {
        OUT.wrong.push(prop + '=' + v + ' refused: ' + String(e.message).slice(0, 40));
    }
}

try {
    var s = findSsl();
    OUT.found = (s !== null);

    if (s) {
        OUT.initTimeout = s.sessionTimeout;
        OUT.initDepth   = s.verifyDepth;

        /* not numbers: must be refused, never taken as 0 */
        mustRefuse(s, 'sessionTimeout', NaN, 'nan');
        mustRefuse(s, 'sessionTimeout', 'abc', 'str');
        mustRefuse(s, 'sessionTimeout', {}, 'obj');
        mustRefuse(s, 'verifyDepth', NaN, 'nan');
        mustRefuse(s, 'verifyDepth', {}, 'obj');
        mustRefuse(s, 'verifyDepth', [1, 2], 'arr2');

        /* out of range */
        mustRefuse(s, 'sessionTimeout', -1, 'neg');
        mustRefuse(s, 'sessionTimeout', 1e18, 'huge');
        mustRefuse(s, 'sessionTimeout', Infinity, 'inf');
        mustRefuse(s, 'verifyDepth', -1, 'neg');
        mustRefuse(s, 'verifyDepth', 2147483648, 'int32overflow');
        mustRefuse(s, 'verifyDepth', -Infinity, 'neginf');

        /* and the legal values still work -- a validator that refused
         * everything would satisfy the list above on its own */
        mustAccept(s, 'sessionTimeout', 0);
        mustAccept(s, 'sessionTimeout', 1);
        mustAccept(s, 'sessionTimeout', 600);
        mustAccept(s, 'sessionTimeout', 2147483647);
        mustAccept(s, 'verifyDepth', 0);
        mustAccept(s, 'verifyDepth', 1);
        mustAccept(s, 'verifyDepth', 10);
        mustAccept(s, 'verifyDepth', 2147483647);

        /* The policy is plain ToNumber, then refuse non-finite and
         * out-of-range.  That admits the coercions ToNumber itself admits --
         * Number([]) is 0 and Number("600") is 600 -- and they are pinned here
         * rather than left to be rediscovered as surprises.  Rejecting them
         * would mean a stricter "must be typeof number" rule, which is a
         * different policy, not a stronger version of this one. */
        s.verifyDepth = [];
        OUT.bools.emptyArrIsZero = (s.verifyDepth === 0);
        s.sessionTimeout = '600';
        OUT.bools.numericStrWorks = (s.sessionTimeout === 600);

        /* booleans coerce, deliberately: that is what a boolean property does */
        s.sessionTickets = {};
        OUT.bools.objIsTrue = (s.sessionTickets === true);
        s.preferServerCiphers = '';
        OUT.bools.emptyStrIsFalse = (s.preferServerCiphers === false);
        s.sessionTickets = 1;
        OUT.bools.oneIsTrue = (s.sessionTickets === true);

        /* put the server back the way the config had it */
        try { s.sessionTimeout = 600; s.verifyDepth = 2;
              s.sessionTickets = true; s.preferServerCiphers = false; }
        catch (e) { OUT.restoreFailed = String(e.message).slice(0, 60); }

        OUT.ok = true;
    }
} catch (e) { OUT.error = String(e && e.message); }

var ss = nginx.http.servers;
for (var i = 0; i < ss.length; i++) {
    var locs = ss[i].locations;
    for (var j = 0; j < locs.length; j++) {
        if (locs[j].path === '/r') {
            locs[j].handler = function (req) {
                req.respond(200, { 'content-type': 'application/json' },
                            JSON.stringify(OUT));
            };
        }
    }
}
JS

$t->try_run('no js module')->plan(11);

my $r = http_get('/r', socket => IO::Socket::INET->new(
    Proto => 'tcp', PeerAddr => '127.0.0.1:' . port(8081)));
my ($body) = $r =~ /\r\n\r\n(.*)/s;
my $j = $body ? eval { decode_json($body) } : undef;

ok($j, 'probe responded') or diag substr($body // 'no body', 0, 200);

SKIP: {
    skip 'no response', 10 unless $j;

    ok($j->{found}, 'the ssl server was reachable through the COM tree')
        or diag 'error: ' . ($j->{error} // 'none');

    diag "refused:  " . join(', ', @{ $j->{refused}  || [] });
    diag "accepted: " . join(', ', @{ $j->{accepted} || [] });

    is(scalar @{ $j->{wrong} || ['?'] }, 0,
       'every bad value refused, every legal one stored and read back')
        or diag '  ' . join("\n  ", @{ $j->{wrong} || [] });

    # Work verification: both halves must have run, or "nothing wrong" is the
    # report of a probe that did nothing.
    is(scalar @{ $j->{refused}  || [] }, 12, 'all twelve bad values were tried');
    is(scalar @{ $j->{accepted} || [] }, 8,  'all eight legal values were tried');

    my $b = $j->{bools} || {};
    ok($b->{objIsTrue},       'boolean property: an object is true (ToBool, by design)');
    ok($b->{emptyStrIsFalse}, 'boolean property: an empty string is false');
    ok($b->{oneIsTrue},       'boolean property: 1 is true');
    ok($b->{emptyArrIsZero},  'numeric property: [] is 0 (plain ToNumber, pinned)');
    ok($b->{numericStrWorks}, 'numeric property: "600" is 600 (plain ToNumber, pinned)');

    ok(!$j->{restoreFailed}, 'the configured values could be restored')
        or diag $j->{restoreFailed};
}
