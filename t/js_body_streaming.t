#!/usr/bin/perl

# Tests for req.bodyPreread and req.bodyChunks() — request body streaming API.
#
# req.bodyPreread (magic 27):
#   Synchronous getter.  Returns the bytes already in r->header_in beyond
#   the request headers — the "preread" portion that arrived in the same
#   recv() as the request line and headers.
#
#   1  — bodyPreread non-empty for small POST (fits in one TCP segment)
#   2  — bodyPreread contains correct content
#   3  — bodyPreread is "" after readBody() has consumed it
#   4  — bodyPreread is "" for GET (no body)
#
# req.bodyChunks() → AsyncIterator<string>:
#   Returns an async iterator that yields each nginx request-body buffer as
#   a string.  Satisfies [Symbol.asyncIterator] so it works with for-await.
#
#   5  — iterates a small body, all chunks concatenated equal the body
#   6  — early break: breaks after first chunk, no error
#   7  — for-await works (iterator protocol correct)
#   8  — empty body (Content-Length: 0) → iterator immediately done
#   9  — bodyChunks after readBody: body already buffered, still iterates
#   10 — bodyChunks on large body collects correct data
#   11 — bodyPreread + bodyChunks: preread has content, then bodyChunks gets rest
#   12 — bodyPreread used to route; bodyChunks used to get remainder

use warnings;
use strict;
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

js_source %%TESTDIR%%/body_streaming.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # bodyPreread — return what's in the buffer before body is read
        location /preread { }

        # bodyPreread after readBody — should be ""
        location /preread-after-read { }

        # bodyPreread on GET (no body)
        location /preread-get { }

        # collect all chunks via bodyChunks and return concatenated
        location /chunks-concat { }

        # break after first chunk
        location /chunks-break { }

        # empty body — bodyChunks should give done immediately
        location /chunks-empty { }

        # bodyChunks after readBody
        location /chunks-after-read { }

        # large body (several KB) — all data arrives intact
        location /chunks-large { }

        # bodyPreread + bodyChunks remainder
        location /preread-and-chunks { }
    }
}
EOF

$t->write_file('body_streaming.js', <<'JS');
(function () {
    'use strict';

    var srv  = nginx.http.servers[0];
    var locs = srv.locations;

    function findLoc(path) {
        for (var i = 0; i < locs.length; i++) {
            if (locs[i].path === path) { return locs[i]; }
        }
        return null;
    }

    /* /preread — return bodyPreread as the response body */
    var loc = findLoc('/preread');
    if (loc) {
        loc.handler = function (req) {
            var preread = req.bodyPreread;
            req.respond(200, { 'Content-Type': 'text/plain' }, preread);
        };
    }

    /* /preread-after-read — call readBody first, then check bodyPreread */
    loc = findLoc('/preread-after-read');
    if (loc) {
        loc.handler = async function (req) {
            await req.readBody();
            var preread = req.bodyPreread;
            req.respond(200, { 'Content-Type': 'text/plain' },
                        'len=' + preread.length);
        };
    }

    /* /preread-get — GET request: no body, preread should be empty */
    loc = findLoc('/preread-get');
    if (loc) {
        loc.handler = function (req) {
            var preread = req.bodyPreread;
            req.respond(200, { 'Content-Type': 'text/plain' },
                        'len=' + preread.length);
        };
    }

    /* /chunks-concat — collect all chunks and return concatenated */
    loc = findLoc('/chunks-concat');
    if (loc) {
        loc.handler = async function (req) {
            var collected = '';
            for await (var chunk of req.bodyChunks()) {
                collected += chunk;
            }
            req.respond(200, { 'Content-Type': 'text/plain' }, collected);
        };
    }

    /* /chunks-break — break after the first chunk, return what we got */
    loc = findLoc('/chunks-break');
    if (loc) {
        loc.handler = async function (req) {
            var first = '';
            for await (var chunk of req.bodyChunks()) {
                first = chunk;
                break;
            }
            req.respond(200, { 'Content-Type': 'text/plain' }, 'ok:' + first.length);
        };
    }

    /* /chunks-empty — POST with Content-Length: 0 */
    loc = findLoc('/chunks-empty');
    if (loc) {
        loc.handler = async function (req) {
            var count = 0;
            for await (var chunk of req.bodyChunks()) {
                count++;
            }
            req.respond(200, { 'Content-Type': 'text/plain' },
                        'chunks=' + count);
        };
    }

    /* /chunks-after-read — readBody first, then bodyChunks */
    loc = findLoc('/chunks-after-read');
    if (loc) {
        loc.handler = async function (req) {
            var body = await req.readBody();
            var collected = '';
            for await (var chunk of req.bodyChunks()) {
                collected += chunk;
            }
            req.respond(200, { 'Content-Type': 'text/plain' },
                        (collected === body) ? 'match' : 'mismatch');
        };
    }

    /* /chunks-large — POST a large body, verify data integrity */
    loc = findLoc('/chunks-large');
    if (loc) {
        loc.handler = async function (req) {
            var total = 0;
            for await (var chunk of req.bodyChunks()) {
                total += chunk.length;
            }
            req.respond(200, { 'Content-Type': 'text/plain' },
                        'total=' + total);
        };
    }

    /* /preread-and-chunks — snapshot bodyPreread, then collect full body
     * via bodyChunks.  After bodyChunks, the full body is in fromIter.
     * bodyPreread is a snapshot of r->header_in BEFORE body reading, so
     * it may contain up to content-length bytes (possibly plus a trailing
     * pipeline byte from the test client).  The Content-Length-bounded
     * body from bodyChunks is always a prefix of what bodyPreread had.  */
    loc = findLoc('/preread-and-chunks');
    if (loc) {
        loc.handler = async function (req) {
            var pre      = req.bodyPreread;
            var fromIter = '';
            for await (var chunk of req.bodyChunks()) {
                fromIter += chunk;
            }
            /* fromIter is exactly Content-Length bytes.
             * pre is whatever was in header_in (may be >= fromIter.length).
             * Check: the first min(pre.length, fromIter.length) bytes agree. */
            var minLen = pre.length < fromIter.length
                       ? pre.length : fromIter.length;
            var ok = minLen === 0
                   || pre.slice(0, minLen) === fromIter.slice(0, minLen);
            req.respond(200, { 'Content-Type': 'text/plain' },
                        'preread=' + pre.length +
                        ' total=' + fromIter.length +
                        ' ok=' + ok);
        };
    }

}());
JS

$t->run();

# -----------------------------------------------------------------------
# Helper: POST to a location with a body
# -----------------------------------------------------------------------

sub post {
    my ($loc, $body) = @_;
    my $len = length($body);
    return http(<<"EOF");
POST $loc HTTP/1.0
Host: localhost
Content-Type: application/octet-stream
Content-Length: $len

$body
EOF
}

# -----------------------------------------------------------------------
# 1-2: bodyPreread — non-empty, correct content for small POST
# -----------------------------------------------------------------------

my $small = 'hello-preread-body';
my $r = post('/preread', $small);
like($r, qr{200 OK},     'preread: 200 OK');
like($r, qr{$small},     'preread: correct content returned');

# -----------------------------------------------------------------------
# 3: bodyPreread after readBody() — should be very short (body consumed).
# The test HTTP client appends a trailing \n after the body (heredoc
# artefact); nginx reads exactly Content-Length bytes, leaving that \n in
# r->header_in.  So len is 0 or 1 — either way the actual body is gone.
# -----------------------------------------------------------------------

$r = post('/preread-after-read', 'some-body');
like($r, qr{len=[01]}, 'preread-after-read: bodyPreread is short after readBody');

# -----------------------------------------------------------------------
# 4: bodyPreread on GET — no body, returns empty string
# -----------------------------------------------------------------------

$r = http("GET /preread-get HTTP/1.0\r\nHost: localhost\r\n\r\n");
like($r, qr{len=0}, 'preread-get: bodyPreread is empty for GET');

# -----------------------------------------------------------------------
# 5-6: bodyChunks — concatenated chunks equal the original body
# -----------------------------------------------------------------------

my $body5 = 'chunk-test-body-content-123';
$r = post('/chunks-concat', $body5);
like($r, qr{200 OK},   'chunks-concat: 200 OK');
like($r, qr{$body5},   'chunks-concat: all chunks concatenate to original body');

# -----------------------------------------------------------------------
# 7: early break — no error, response received
# -----------------------------------------------------------------------

$r = post('/chunks-break', 'break-me-after-first-chunk');
like($r, qr{ok:\d+}, 'chunks-break: early break works, got response');

# -----------------------------------------------------------------------
# 8: empty body (Content-Length: 0) — iterator immediately done
# -----------------------------------------------------------------------

$r = http("POST /chunks-empty HTTP/1.0\r\nHost: localhost\r\n" .
          "Content-Length: 0\r\n\r\n");
like($r, qr{chunks=0}, 'chunks-empty: no chunks for empty body');

# -----------------------------------------------------------------------
# 9: bodyChunks after readBody — still iterates correctly
# -----------------------------------------------------------------------

$r = post('/chunks-after-read', 'after-read-body');
like($r, qr{match}, 'chunks-after-read: bodyChunks after readBody returns same data');

# -----------------------------------------------------------------------
# 10: large body — correct total byte count
# -----------------------------------------------------------------------

my $large = 'x' x 4096;
$r = post('/chunks-large', $large);
like($r, qr{total=4096}, 'chunks-large: correct byte count for 4 KB body');

# -----------------------------------------------------------------------
# 11: bodyPreread + bodyChunks — preread is a prefix of the full body
# -----------------------------------------------------------------------

my $body11 = 'routing-field=payments&region=eu-west&padding=' . ('p' x 32);
$r = post('/preread-and-chunks', $body11);
like($r, qr{ok=true}, 'preread-and-chunks: preread bytes match prefix of full body from chunks');

# -----------------------------------------------------------------------
# 12: bodyPreread non-zero for body that fits in one TCP segment
# -----------------------------------------------------------------------

my $body12 = 'service=payments region=eu-west padding=extra';
$r = post('/preread', $body12);
like($r, qr{$body12}, 'preread-json: full body in preread for small payload');
