#!/usr/bin/perl

# Tests for JS-Pilgrim P14: Upstream JS filters.
#
# location.addUpstreamFilter(async function*(chunks, req) {...})
#   Filters the upstream response body; only runs when r->upstream is set.
#   chunks is a single-element iterable [wholeBodyString].
#
# location.addUpstreamRequestFilter(async function*(chunks, req) {...})
#   Transforms the client request body before it is forwarded to the upstream.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(20);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/p14_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    # Back-end server: static response + JS echo handler
    server {
        listen       127.0.0.1:%%PORT_8082%%;
        server_name  backend;

        location /plain/ {
            return 200 "hello from upstream\n";
        }

        location /echo/ { }
    }

    # Front-end server: proxy_pass to back-end, JS filters wired via init.js
    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /plain/ { proxy_pass http://127.0.0.1:%%PORT_8082%%/plain/; }
        location /resp/  { proxy_pass http://127.0.0.1:%%PORT_8082%%/plain/; }
        location /chain/ { proxy_pass http://127.0.0.1:%%PORT_8082%%/plain/; }
        location /req/   { proxy_pass http://127.0.0.1:%%PORT_8082%%/echo/;  }
        location /both/  { proxy_pass http://127.0.0.1:%%PORT_8082%%/echo/;  }
    }
}
EOF

$t->write_file_expand('p14_init.js', <<'JS');
// JS-Pilgrim P14 — upstream filter tests

// Back-end server is servers[0]; wire /echo/ to a JS request-body echo handler
(function() {
    var locs = nginx.http.servers[0].locations;
    for (var i = 0; i < locs.length; i++) {
        if (locs[i].path === '/echo/') {
            locs[i].handler = async function(req) {
                var body = await req.readBody();
                req.respond(200, {'Content-Type': 'text/plain'}, body || '');
            };
        }
    }
})();

// Front-end server is servers[1]; register upstream filters by location path
(function() {
    var locs = nginx.http.servers[1].locations;
    var by = {};
    for (var i = 0; i < locs.length; i++) { by[locs[i].path] = locs[i]; }

    // /resp/ — replace "hello" with "world" in upstream response
    by['/resp/'].addUpstreamFilter(async function*(chunks, req) {
        for await (var chunk of chunks) {
            yield chunk.replace(/hello/g, 'world');
        }
    });

    // /chain/ — uppercase, then add prefix
    by['/chain/'].addUpstreamFilter(async function*(chunks, req) {
        for await (var chunk of chunks) { yield chunk.toUpperCase(); }
    });
    by['/chain/'].addUpstreamFilter(async function*(chunks, req) {
        for await (var chunk of chunks) {
            yield chunk.replace(/HELLO/g, 'FILTERED:HELLO');
        }
    });

    // /req/ — uppercase the request body before it goes upstream
    by['/req/'].addUpstreamRequestFilter(async function*(chunks, req) {
        for await (var chunk of chunks) { yield chunk.toUpperCase(); }
    });

    // /both/ — uppercase request body, and replace in response
    by['/both/'].addUpstreamRequestFilter(async function*(chunks, req) {
        for await (var chunk of chunks) { yield chunk.toUpperCase(); }
    });
    by['/both/'].addUpstreamFilter(async function*(chunks, req) {
        for await (var chunk of chunks) {
            yield chunk.replace(/HELLO/g, 'WORLD');
        }
    });
})();
JS

$t->run();

# -----------------------------------------------------------------------
# 1–4: Baseline — no filter, proxy_pass works normally
# -----------------------------------------------------------------------

my $r = http_get('/plain/');
like($r, qr{200 OK},                'plain: 200 OK');
like($r, qr{hello from upstream}m,  'plain: body unchanged');

# -----------------------------------------------------------------------
# 5–8: addUpstreamFilter — response body transformed
# -----------------------------------------------------------------------

$r = http_get('/resp/');
like($r, qr{200 OK},                'resp filter: 200 OK');
like($r, qr{world from upstream}m,  'resp filter: hello replaced with world');

$r = http_get('/resp/');
like($r, qr{200 OK},                'resp filter 2: 200 OK');
like($r, qr{world from upstream}m,  'resp filter 2: replacement persists');

# -----------------------------------------------------------------------
# 9–12: Chained addUpstreamFilter
# -----------------------------------------------------------------------

$r = http_get('/chain/');
like($r, qr{200 OK},                        'chain: 200 OK');
like($r, qr{FILTERED:HELLO FROM UPSTREAM}m, 'chain: both filters applied');

$r = http_get('/chain/');
like($r, qr{200 OK},                        'chain 2: 200 OK');
like($r, qr{FILTERED:HELLO FROM UPSTREAM}m, 'chain 2: second request same result');

# -----------------------------------------------------------------------
# 13–16: addUpstreamRequestFilter — request body transformed
# -----------------------------------------------------------------------

$r = http_post('/req/', "hello world\n");
like($r, qr{200 OK},       'req filter: 200 OK');
like($r, qr{HELLO WORLD}m, 'req filter: body uppercased before upstream');

$r = http_post('/req/', "foo bar\n");
like($r, qr{200 OK},   'req filter 2: 200 OK');
like($r, qr{FOO BAR}m, 'req filter 2: different body uppercased');

# -----------------------------------------------------------------------
# 17–20: Both filters together
# -----------------------------------------------------------------------

$r = http_post('/both/', "hello\n");
like($r, qr{200 OK},  'both: 200 OK');
like($r, qr{WORLD}m,  'both: request uppercased then response filtered');

$r = http_post('/both/', "hello again\n");
like($r, qr{200 OK},  'both 2: 200 OK');
like($r, qr{WORLD}m,  'both 2: repeatable');

# -----------------------------------------------------------------------
# 21–22: Auto checks
# -----------------------------------------------------------------------

ok($t->waitforsocket('127.0.0.1:' . port(8082)), 'backend started without crash');
unlike($t->read_file('error.log'), qr/alert|emerg/, 'no alerts in error.log');


sub http_post {
    my ($url, $body) = @_;
    return http(
        "POST $url HTTP/1.0\r\n"
      . "Host: localhost\r\n"
      . "Content-Length: " . length($body) . "\r\n"
      . "\r\n"
      . $body
    );
}
