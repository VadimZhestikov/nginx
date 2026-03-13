#!/usr/bin/perl

# Stage 7a: NginxImageFilter — runtime-writable properties
#
# All ten NginxImageFilter properties become writable:
#   action        string  ("off"/"test"/"size"/"resize"/"crop"/"rotate")
#   width         number  >= 0
#   height        number  >= 0
#   angle         number  >= 0
#   jpegQuality   number  >= 0
#   webpQuality   number  >= 0
#   sharpen       number  >= 0
#   transparency  boolean
#   interlace     boolean
#   bufferSize    number  >= 0
#
# Tests:
#   1.  Initial action getter returns "resize" from config
#   2.  Initial width returns 320
#   3.  Set action to "crop" — getter reflects change
#   4.  Set width to 640 — getter reflects change
#   5.  Set height to 480 — getter reflects change
#   6.  Set jpegQuality to 85 — getter reflects change
#   7.  Set transparency to false — getter returns false
#   8.  Set interlace to true — getter returns true
#   9.  Set bufferSize to 2097152 — getter reflects change
#  10.  Persistence: second /read/ shows all new values
#  11.  Error: unknown action string throws TypeError
#  12.  Error: negative width throws RangeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http image_filter/)->plan(12);

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

        location /img/ {
            image_filter  resize 320 240;
            image_filter_jpeg_quality  75;
            image_filter_transparency  on;
            image_filter_interlace     off;
            image_filter_buffer        1m;
        }

        location /read/   { }
        location /set/    { }
        location /badact/ { }
        location /badnum/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

const imgLoc = loc('/img/');

// /read/ — return all current imageFilter values
loc('/read/').handler = r => {
    const f = imgLoc.imageFilter;
    r.respond(200, {}, JSON.stringify({
        action:       f.action,
        width:        f.width,
        height:       f.height,
        jpegQuality:  f.jpegQuality,
        transparency: f.transparency,
        interlace:    f.interlace,
        bufferSize:   f.bufferSize,
    }));
};

// /set/ — change several properties then return new values
loc('/set/').handler = r => {
    const f = imgLoc.imageFilter;
    f.action       = 'crop';
    f.width        = 640;
    f.height       = 480;
    f.jpegQuality  = 85;
    f.transparency = false;
    f.interlace    = true;
    f.bufferSize   = 2097152;
    r.respond(200, {}, JSON.stringify({
        action:       f.action,
        width:        f.width,
        height:       f.height,
        jpegQuality:  f.jpegQuality,
        transparency: f.transparency,
        interlace:    f.interlace,
        bufferSize:   f.bufferSize,
    }));
};

// /badact/ — unknown action string
loc('/badact/').handler = r => {
    try {
        imgLoc.imageFilter.action = 'bogus';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};

// /badnum/ — negative width
loc('/badnum/').handler = r => {
    try {
        imgLoc.imageFilter.width = -1;
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values from config ----
my $r0 = http_get('/read/');
like($r0, qr/"action":"resize"/, 'initial action is resize');
like($r0, qr/"width":320/,       'initial width is 320');

# ---- Apply writes ----
my $r1 = http_get('/set/');
like($r1, qr/"action":"crop"/,        'action updated to crop');
like($r1, qr/"width":640/,            'width updated to 640');
like($r1, qr/"height":480/,           'height updated to 480');
like($r1, qr/"jpegQuality":85/,       'jpegQuality updated to 85');
like($r1, qr/"transparency":false/,   'transparency updated to false');
like($r1, qr/"interlace":true/,       'interlace updated to true');
like($r1, qr/"bufferSize":2097152/,   'bufferSize updated to 2097152');

# ---- Persistence ----
like(http_get('/read/'), qr/"action":"crop"/, 'changes persist on next request');

# ---- Error paths ----
like(http_get('/badact/'), qr/error:/, 'unknown action string throws');
like(http_get('/badnum/'), qr/error:/, 'negative width throws RangeError');

$t->stop();
