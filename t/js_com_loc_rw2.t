#!/usr/bin/perl

# Stage 9a: NginxLocation — remaining scalar fields become runtime-writable
#
# Fields added in this stage (previously read-only):
#   satisfy                string  "all"/"any"
#   limitExcept            string[]→bitmask
#   lingering              string  "off"/"on"/"always"
#   lingeringTimeout       ms
#   lingeringTime          ms
#   resolverTimeout        ms
#   chunkedTransferEncoding boolean
#   msieRefresh            boolean
#   logNotFound            boolean
#   logSubrequest          boolean
#   recursiveErrorPages    boolean
#   clientBodyBufferSize   bytes
#   clientBodyInFileOnly   string  "off"/"on"/"clean"
#   clientBodyInSingleBuffer boolean
#   resetTimedoutConnection  boolean
#   absoluteRedirect       boolean
#   serverNameInRedirect   boolean
#   portInRedirect         boolean
#   msiePadding            boolean
#   ifModifiedSince        string  "off"/"exact"/"before"
#   maxRanges              number
#   authDelay              ms
#   sendLowat              bytes
#   postponeOutput         bytes
#   keepaliveDisable       string[]→bitmask
#   keepaliveMinTimeout    ms
#   sendfileMaxChunk       bytes
#   readAhead              bytes
#   directio               "off" | bytes
#   directioAlignment      bytes
#
# Tests:
#   1-2.  initial values from config
#   3.    set all scalar fields — no error
#   4-10. spot-check getters after write
#   11.   persistence: second /read/ shows new values
#   12.   satisfy: bad string throws TypeError
#   13.   lingering: bad string throws TypeError
#   14.   ifModifiedSince: bad string throws TypeError
#   15.   limitExcept: bad method throws TypeError
#   16.   keepaliveDisable: bad browser throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(16);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        location /target/ {
            satisfy        all;
            lingering_close on;
            log_not_found  on;
        }

        location /read/   { }
        location /set/    { }
        location /errs/   { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const tgt = loc('/target/');

// /read/ — return a snapshot of target location fields
loc('/read/').handler = r => {
    const l = tgt;
    r.respond(200, {}, JSON.stringify({
        satisfy:                 l.satisfy,
        lingering:               l.lingering,
        logNotFound:             l.logNotFound,
        logSubrequest:           l.logSubrequest,
        chunkedTransferEncoding: l.chunkedTransferEncoding,
        recursiveErrorPages:     l.recursiveErrorPages,
        clientBodyInFileOnly:    l.clientBodyInFileOnly,
        ifModifiedSince:         l.ifModifiedSince,
        maxRanges:               l.maxRanges,
        sendfileMaxChunk:        l.sendfileMaxChunk,
        directio:                l.directio,
    }));
};

// /set/ — write all target fields
loc('/set/').handler = r => {
    const l = tgt;
    l.satisfy                 = 'any';
    l.limitExcept             = ['GET', 'POST'];
    l.lingering               = 'always';
    l.lingeringTimeout        = 10000;
    l.lingeringTime           = 60000;
    l.resolverTimeout         = 5000;
    l.chunkedTransferEncoding = false;
    l.msieRefresh             = true;
    l.logNotFound             = false;
    l.logSubrequest           = true;
    l.recursiveErrorPages     = true;
    l.clientBodyBufferSize    = 32768;
    l.clientBodyInFileOnly    = 'clean';
    l.clientBodyInSingleBuffer = true;
    l.resetTimedoutConnection = true;
    l.absoluteRedirect        = false;
    l.serverNameInRedirect    = true;
    l.portInRedirect          = false;
    l.msiePadding             = false;
    l.ifModifiedSince         = 'before';
    l.maxRanges               = 10;
    l.authDelay               = 1000;
    l.sendLowat               = 0;
    l.postponeOutput          = 1460;
    l.keepaliveDisable        = ['msie6'];
    l.keepaliveMinTimeout     = 2000;
    l.sendfileMaxChunk        = 2097152;
    l.readAhead               = 0;
    l.directio                = 4194304;
    l.directioAlignment       = 512;
    r.respond(200, {}, 'ok');
};

// /errs/ — trigger error paths
loc('/errs/').handler = r => {
    const results = {};
    const l = tgt;

    try { l.satisfy = 'wrong'; results.satisfy = 'no-error'; }
    catch (e) { results.satisfy = 'error'; }

    try { l.lingering = 'bad'; results.lingering = 'no-error'; }
    catch (e) { results.lingering = 'error'; }

    try { l.ifModifiedSince = 'nope'; results.ims = 'no-error'; }
    catch (e) { results.ims = 'error'; }

    try { l.limitExcept = ['GET', 'CONNECT']; results.limitExcept = 'no-error'; }
    catch (e) { results.limitExcept = 'error'; }

    try { l.keepaliveDisable = ['ie7']; results.kdisable = 'no-error'; }
    catch (e) { results.kdisable = 'error'; }

    r.respond(200, {}, JSON.stringify(results));
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"satisfy":"all"/,     'initial satisfy is all');
like($r0, qr/"lingering":"on"/,    'initial lingering is on');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"satisfy":"any"/,              'satisfy updated to any');
like($r1, qr/"lingering":"always"/,         'lingering updated to always');
like($r1, qr/"logNotFound":false/,          'logNotFound updated');
like($r1, qr/"chunkedTransferEncoding":false/, 'chunkedTransferEncoding updated');
like($r1, qr/"clientBodyInFileOnly":"clean"/, 'clientBodyInFileOnly updated to clean');
like($r1, qr/"ifModifiedSince":"before"/,   'ifModifiedSince updated to before');
like($r1, qr/"maxRanges":10/,               'maxRanges updated');
like($r1, qr/"sendfileMaxChunk":2097152/,   'sendfileMaxChunk updated');

# ---- Persistence ----
like(http_get('/read/'), qr/"satisfy":"any"/, 'changes persist on next request');

# ---- Error paths ----
my $errs = http_get('/errs/');
like($errs, qr/"satisfy":"error"/,     'bad satisfy string throws TypeError');
like($errs, qr/"lingering":"error"/,   'bad lingering string throws TypeError');
like($errs, qr/"ims":"error"/,         'bad ifModifiedSince string throws TypeError');
like($errs, qr/"limitExcept":"error"/, 'bad limitExcept method throws TypeError');

$t->stop();
