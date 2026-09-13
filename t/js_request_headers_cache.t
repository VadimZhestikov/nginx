#!/usr/bin/perl

# req.headers is materialized ONCE per request (HOST-PERF).
#
# The getter used to rebuild the object on every access -- 0.130 us against
# 0.029 us for a hoisted local, so ~0.10 us per access was rebuilding a surface
# that cannot change (t/tools/host-call-cost.t). It is now built on first access
# and held on the request's opaque.
#
# That buys one new way to be catastrophically wrong, and it is what this file
# is mostly about: a cached per-request surface that OUTLIVES its request would
# serve one client's headers to the next. The Authorization header below is the
# blunt version of that question. Identity within a request is the cheap part;
# isolation ACROSS requests is the part that must never regress, so it is
# checked with distinct values per request on a keepalive connection and again
# on separate connections.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /h { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;

locs.find(function (l) { return l.path === "/h"; }).handler = function (req) {
    var a = req.headers;
    var b = req.headers;

    /* mutating the surface is now observable within the request; it used to be
       written to a throwaway object, which is worse (a silent no-op) */
    a['x-scratch'] = 'written';

    req.respond(200, {'content-type':'application/json'}, JSON.stringify({
        same:      (a === b),
        marker:    req.headers['x-marker'] || 'ABSENT',
        auth:      req.headers['authorization'] || 'ABSENT',
        scratch:   req.headers['x-scratch'] || 'ABSENT',
        count:     Object.keys(req.headers).length
    }));
};
JS

$t->try_run('no js module')->plan(8);

###############################################################################

my $r1 = http(<<'EOF');
GET /h HTTP/1.0
Host: localhost
X-Marker: first
Authorization: Bearer AAA

EOF

like($r1, qr/"same":true/,
     'the two reads inside one request return the SAME object -- the surface '
     . 'is materialized once, not per access');
like($r1, qr/"marker":"first"/, 'the request sees its own header value');
like($r1, qr/"scratch":"written"/,
     'a write to req.headers is now visible through a later read of it; '
     . 'before caching it went to a throwaway object and vanished silently');

my $r2 = http(<<'EOF');
GET /h HTTP/1.0
Host: localhost
X-Marker: second
Authorization: Bearer BBB

EOF

like($r2, qr/"marker":"second"/,
     'a LATER request on a new connection sees its own marker, not the '
     . 'previous one -- the cache dies with its request');
like($r2, qr/"auth":"Bearer BBB"/,
     'and its own Authorization header: a per-request surface that outlived '
     . 'its request would hand one client the credentials of another');
unlike($r2, qr/AAA/,
     'no value from the first request appears anywhere in the second response');

# keepalive: two requests over ONE connection, where a stale cache is likeliest
my $s = IO::Socket::INET->new(PeerAddr => '127.0.0.1', PeerPort => port(8080),
                              Proto => 'tcp')
        or die "connect: $!";
print $s "GET /h HTTP/1.1\r\nHost: localhost\r\nX-Marker: ka-one\r\n"
       . "Authorization: Bearer KA1\r\n\r\n";
my $ka1 = '';
$ka1 .= $_ while defined($_ = eval { local $SIG{ALRM} = sub { die }; alarm 2;
                                     my $b; sysread($s, $b, 4096); alarm 0; $b })
        && length($_) && $ka1 !~ /\}/;
print $s "GET /h HTTP/1.1\r\nHost: localhost\r\nX-Marker: ka-two\r\n"
       . "Authorization: Bearer KA2\r\nConnection: close\r\n\r\n";
my $ka2 = '';
$ka2 .= $_ while defined($_ = eval { local $SIG{ALRM} = sub { die }; alarm 2;
                                     my $b; sysread($s, $b, 4096); alarm 0; $b })
        && length($_);
close $s;

like($ka1, qr/"marker":"ka-one"/, 'keepalive: the first request sees its own headers');
like($ka2 . $ka1, qr/"marker":"ka-two"/,
     'keepalive: the SECOND request on the same connection sees its own '
     . 'headers -- the request object is rebuilt per request, so the surface '
     . 'held on it cannot survive into the next one');
