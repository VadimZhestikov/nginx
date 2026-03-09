#!/usr/bin/perl

# Tests for Stage 29 COM expansion: r.body and r.readBody()
#
# r.body        — request body string if already read, else null
# r.readBody()  — Promise<string>; triggers nginx body reading

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

js_include %%TESTDIR%%/init_body.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # GET — no body; r.body must be null before readBody
        location /get_no_body { }

        # POST — async handler reads body with readBody()
        location /post_body { }

        # POST — r.body before readBody is null; after readBody is string
        location /body_before_after { }
    }
}
EOF

$t->write_file('init_body.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/get_no_body',       getNoBody);
    set('/post_body',         postBody);
    set('/body_before_after', bodyBeforeAfter);
})();

function getNoBody(r) {
    /* GET request, no body — r.body should be null */
    const b = r.body;
    r.respond(200, {'content-type': 'text/plain'},
              b === null ? 'PASS' : 'FAIL:' + JSON.stringify(b));
}

async function postBody(r) {
    const body = await r.readBody();
    r.respond(200, {'content-type': 'text/plain'}, body);
}

async function bodyBeforeAfter(r) {
    /* r.body is null before readBody() */
    const before = r.body;
    const body   = await r.readBody();
    /* r.body is now the body string */
    const after  = r.body;
    const ok = before === null
            && typeof body === 'string'
            && typeof after === 'string'
            && body === after;
    r.respond(200, {'content-type': 'text/plain'},
              ok ? 'PASS' : 'FAIL:' + JSON.stringify({before, body, after}));
}
JS

$t->try_run('no js module')->plan(5);

# GET — no body → r.body is null
like(http_get('/get_no_body'), qr/PASS/, 'r.body: null for GET (no body)');

# POST — readBody() echoes the body
my $post = sub {
    my ($url, $body) = @_;
    return http(
        "POST $url HTTP/1.0\r\n"
        . "Host: localhost\r\n"
        . "Content-Length: " . length($body) . "\r\n"
        . "\r\n"
        . $body
    );
};

like($post->('/post_body', 'hello world'), qr/hello world/,
     'r.readBody(): echoes POST body');

like($post->('/post_body', ''), qr/200 OK/,
     'r.readBody(): empty body returns 200');

like($post->('/body_before_after', 'test'), qr/PASS/,
     'r.body: null before readBody, string after');

# Large-ish body (4 KB)
my $big = 'x' x 4096;
like($post->('/post_body', $big), qr/$big/,
     'r.readBody(): 4 KB body echoed correctly');
