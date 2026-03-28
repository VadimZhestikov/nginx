#!/usr/bin/perl

# Tests for JS-Pilgrim P6: listener.addL4Filter(asyncGenFn)
#
# The filter intercepts raw TCP bytes before HTTP parsing.
# Each test connects via a raw TCP socket (IO::Socket::INET) so it can
# send a custom byte prefix that the L4 filter strips before nginx sees it.

use warnings;
use strict;
use IO::Socket::INET;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p6_l4_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  base;
        location /base/ { }
        location /l4/   { }
        location /pass/ { }
    }
}
EOF

$t->write_file_expand('p6_l4_init.js', <<'JS');
// JS-Pilgrim P6 — L4 filter test init.

var filterRuns = 0;

var srv = nginx.http.servers[0];

// JS-managed listener on port 8091 with an L4 filter
var sock     = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listener = nginx.http.attach(sock);
listener.addServer(srv);

// L4 filter: strip "FILT:" prefix (5 bytes) from incoming bytes, count runs
listener.addL4Filter(async function*(source) {
    for await (var chunk of source) {
        filterRuns++;
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        if (s.substring(0, 5) === 'FILT:') {
            yield s.substring(5);
        } else {
            yield s;
        }
        return;
    }
});

// Handler for the locations
var locs = srv.locations;
for (var i = 0; i < locs.length; i++) {
    locs[i].handler = function(r) {
        if (r.uri === '/l4/') {
            r.respond(200, {}, 'runs=' + filterRuns);
        } else if (r.uri === '/pass/') {
            r.respond(200, {}, 'pass-ok');
        } else {
            r.respond(200, {}, 'base-ok');
        }
    };
}
JS

$t->run();

# -----------------------------------------------------------------------
# Helper: raw TCP request to host:port, returns response string
# -----------------------------------------------------------------------

sub raw_req {
    my ($port, $data) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto    => 'tcp',
        Timeout  => 3,
    );
    return '' unless $sock;
    print $sock $data;
    local $/;
    my $resp = <$sock>;
    $sock->close();
    return $resp // '';
}

my $p = port(8091);

# -----------------------------------------------------------------------
# 1–2: L4 filter strips "FILT:" prefix; nginx processes the real request
# -----------------------------------------------------------------------

my $r = raw_req($p, "FILT:GET /l4/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'l4 filter: 200 OK after prefix strip');
like($r, qr{runs=1},           'l4 filter: filter ran once');

# -----------------------------------------------------------------------
# 3–4: Second connection — filter counter increments
# -----------------------------------------------------------------------

$r = raw_req($p, "FILT:GET /l4/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'l4 filter: second connection 200');
like($r, qr{runs=2},           'l4 filter: filter ran twice');

# -----------------------------------------------------------------------
# 5–6: No prefix — filter still runs, yields bytes unchanged
# -----------------------------------------------------------------------

$r = raw_req($p, "GET /l4/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'l4 passthrough: 200 OK');
like($r, qr{runs=3},           'l4 passthrough: filter still runs (3 total)');

# -----------------------------------------------------------------------
# 7–8: Port 8080 (no L4 filter) — normal HTTP unaffected
# -----------------------------------------------------------------------

my $resp = http_get('/base/');
like($resp, qr{200 OK},  'no-filter port: 200 OK');
like($resp, qr{base-ok}, 'no-filter port: body correct');

# -----------------------------------------------------------------------
# 9–10: Plain request without prefix through filtered port
# -----------------------------------------------------------------------

$r = raw_req($p, "GET /pass/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'l4 plain: 200 OK');
like($r, qr{pass-ok},          'l4 plain: body correct');

# -----------------------------------------------------------------------
# 11–12: Auto checks — nginx started, no alerts
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
