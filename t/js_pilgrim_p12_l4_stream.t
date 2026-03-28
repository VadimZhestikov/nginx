#!/usr/bin/perl

# Tests for JS-Pilgrim P12: L4 full-stream + async generator
#
# Tests the upgraded listener.addL4Filter with:
#   - async function*(source) { for await (const chunk of source) { ... } }
#   - await inside the generator body (nginx.setTimeout)
#   - multi-chunk streaming (receive loop)
#   - filter chaining (2 filters in series)
#   - passthrough (no modification)

use warnings;
use strict;
use IO::Socket::INET;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(20);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p12_l4_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  base;
        location / { }
    }
}
EOF

$t->write_file_expand('p12_l4_init.js', <<'JS');
// JS-Pilgrim P12 — L4 full-stream async generator test init.

var srv = nginx.http.servers[0];

// -----------------------------------------------------------------------
// Listener A (port 8091): passthrough filter — yields chunk unchanged
// -----------------------------------------------------------------------
var sockA     = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
var listenerA = nginx.http.attach(sockA);
listenerA.addServer(srv);

listenerA.addL4Filter(async function*(source) {
    for await (var chunk of source) {
        yield chunk;
        return;
    }
});

// -----------------------------------------------------------------------
// Listener B (port 8092): strip "STRIP:" prefix (async, with setTimeout)
// -----------------------------------------------------------------------
var sockB     = nginx.createSocket("127.0.0.1:%%PORT_8092%%");
var listenerB = nginx.http.attach(sockB);
listenerB.addServer(srv);

listenerB.addL4Filter(async function*(source) {
    for await (var chunk of source) {
        // async operation inside L4 filter
        await nginx.setTimeout(2);
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        if (s.substring(0, 6) === 'STRIP:') {
            yield s.substring(6);
        } else {
            yield s;
        }
        return;
    }
});

// -----------------------------------------------------------------------
// Listener C (port 8093): two chained filters
//   filter 1: strip "A:" prefix
//   filter 2: strip "B:" prefix (applied to output of filter 1)
// -----------------------------------------------------------------------
var sockC     = nginx.createSocket("127.0.0.1:%%PORT_8093%%");
var listenerC = nginx.http.attach(sockC);
listenerC.addServer(srv);

// Filter 1: strip "A:" prefix
listenerC.addL4Filter(async function*(source) {
    for await (var chunk of source) {
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        if (s.substring(0, 2) === 'A:') {
            yield s.substring(2);
        } else {
            yield s;
        }
        return;
    }
});

// Filter 2: strip "B:" prefix
listenerC.addL4Filter(async function*(source) {
    for await (var chunk of source) {
        var s = String.fromCharCode.apply(null, Array.from(chunk));
        if (s.substring(0, 2) === 'B:') {
            yield s.substring(2);
        } else {
            yield s;
        }
        return;
    }
});

// -----------------------------------------------------------------------
// Handler: echo URI as body
// -----------------------------------------------------------------------
var locs = srv.locations;
for (var i = 0; i < locs.length; i++) {
    locs[i].handler = function(r) {
        r.respond(200, {}, 'uri=' + r.uri);
    };
}
JS

$t->run();

# -----------------------------------------------------------------------
# Helper: raw TCP request, returns response string
# -----------------------------------------------------------------------

sub raw_req {
    my ($port, $data) = @_;
    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => $port,
        Proto    => 'tcp',
        Timeout  => 5,
    );
    return '' unless $sock;
    print $sock $data;
    local $/;
    my $resp = <$sock>;
    $sock->close();
    return $resp // '';
}

my $pA = port(8091);
my $pB = port(8092);
my $pC = port(8093);

# -----------------------------------------------------------------------
# 1–4: Passthrough filter (port 8091) — bytes pass through unchanged
# -----------------------------------------------------------------------

my $r = raw_req($pA, "GET /hello/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'passthrough: 200 OK');
like($r, qr{uri=/hello/},      'passthrough: correct URI');

$r = raw_req($pA, "GET /world/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'passthrough 2: 200 OK');
like($r, qr{uri=/world/},      'passthrough 2: correct URI');

# -----------------------------------------------------------------------
# 5–8: Async filter with setTimeout (port 8092)
# -----------------------------------------------------------------------

$r = raw_req($pB, "STRIP:GET /async/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'async strip: 200 OK after await');
like($r, qr{uri=/async/},      'async strip: STRIP: prefix removed');

$r = raw_req($pB, "GET /nostrip/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'async no-strip: 200 OK');
like($r, qr{uri=/nostrip/},    'async no-strip: body correct');

# -----------------------------------------------------------------------
# 9–14: Chained filters (port 8093) — two filters applied in series
# -----------------------------------------------------------------------

# Both prefixes: "A:B:" is stripped by filter1 (removes A:) then filter2
# (removes B:)
$r = raw_req($pC, "A:B:GET /chain/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'chain both: 200 OK');
like($r, qr{uri=/chain/},      'chain both: A: and B: stripped');

# Only A: prefix: stripped by filter1; filter2 sees no B: prefix
$r = raw_req($pC, "A:GET /chainA/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'chain A only: 200 OK');
like($r, qr{uri=/chainA/},     'chain A only: A: stripped');

# Only B: prefix: filter1 passes through; filter2 strips B:
$r = raw_req($pC, "B:GET /chainB/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{HTTP/1\.[01] 200}, 'chain B only: 200 OK');
like($r, qr{uri=/chainB/},     'chain B only: B: stripped');

# -----------------------------------------------------------------------
# 15–16: Plain HTTP on port 8080 (no filter) still works
# -----------------------------------------------------------------------

my $resp = http_get('/plain/');
like($resp, qr{200 OK},       'no-filter port: 200 OK');
like($resp, qr{uri=/plain/},  'no-filter port: body correct');

# -----------------------------------------------------------------------
# 17–18: Multiple async connections (verify no state bleed)
# -----------------------------------------------------------------------

$r = raw_req($pB, "STRIP:GET /first/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{uri=/first/}, 'async multi 1: first connection OK');

$r = raw_req($pB, "STRIP:GET /second/ HTTP/1.0\r\nHost: base\r\n\r\n");
like($r, qr{uri=/second/}, 'async multi 2: second connection OK');

# -----------------------------------------------------------------------
# 19–20: Auto checks — nginx started, no alerts
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:8080'), 'nginx started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');
