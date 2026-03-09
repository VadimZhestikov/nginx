#!/usr/bin/perl

# Tests for Stage 13n COM expansion: image_filter location configuration
# exposed as properties of location.imageFilter (NginxImageFilter class).
#
# New property on NginxLocation:
#   imageFilter   NginxImageFilter
#
# NginxImageFilter properties (all read-only):
#   action        string  — "off"/"test"/"size"/"resize"/"crop"/"rotate"
#   width         number  — static width arg
#   height        number  — static height arg
#   angle         number  — static rotation angle
#   jpegQuality   number  — image_filter_jpeg_quality
#   webpQuality   number  — image_filter_webp_quality
#   sharpen       number  — image_filter_sharpen
#   transparency  boolean — image_filter_transparency
#   interlace     boolean — image_filter_interlace
#   bufferSize    number  — image_filter_buffer

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

js_include %%TESTDIR%%/init_image_filter.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # default — image_filter off
        location /default {
        }

        # resize with quality + options
        location /resize {
            image_filter              resize 320 240;
            image_filter_jpeg_quality 85;
            image_filter_webp_quality 80;
            image_filter_sharpen      10;
            image_filter_transparency on;
            image_filter_interlace    on;
            image_filter_buffer       2m;
        }

        # rotate
        location /rotate {
            image_filter rotate 90;
        }
    }
}
EOF

$t->write_file('init_image_filter.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.http.servers[0];
// Alphabetical: /default (d) < /resize (r) < /rotate (ro)
const defLoc    = srv.locations[0];
const resizeLoc = srv.locations[1];
const rotateLoc = srv.locations[2];

// ---- default: image_filter off ----
const id = defLoc.imageFilter;
check("id_obj",    typeof id === "object" && id !== null, typeof id);
check("id_action", id.action === "off",                   id.action);

// ---- resize: all options set ----
const ir = resizeLoc.imageFilter;
check("ir_obj",     typeof ir === "object" && ir !== null, typeof ir);
check("ir_action",  ir.action === "resize",                ir.action);
check("ir_width",   ir.width === 320,                      ir.width);
check("ir_height",  ir.height === 240,                     ir.height);
check("ir_jq",      ir.jpegQuality === 85,                 ir.jpegQuality);
check("ir_wq",      ir.webpQuality === 80,                 ir.webpQuality);
check("ir_sharp",   ir.sharpen === 10,                     ir.sharpen);
check("ir_trans",   ir.transparency === true,              ir.transparency);
check("ir_inter",   ir.interlace === true,                 ir.interlace);
check("ir_buf",     ir.bufferSize === 2097152,             ir.bufferSize);

// ---- rotate ----
const iro = rotateLoc.imageFilter;
check("iro_action", iro.action === "rotate",               iro.action);
check("iro_angle",  iro.angle === 90,                      iro.angle);
JS

$t->try_run('no image_filter module')->plan(14);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS id_obj/,     'location.imageFilter is object');
like($log, qr/JSTEST PASS id_action/,  'default: action == "off"');
like($log, qr/JSTEST PASS ir_obj/,     'resize: location.imageFilter is object');
like($log, qr/JSTEST PASS ir_action/,  'resize: action == "resize"');
like($log, qr/JSTEST PASS ir_width/,   'resize: width == 320');
like($log, qr/JSTEST PASS ir_height/,  'resize: height == 240');
like($log, qr/JSTEST PASS ir_jq/,      'resize: jpegQuality == 85');
like($log, qr/JSTEST PASS ir_wq/,      'resize: webpQuality == 80');
like($log, qr/JSTEST PASS ir_sharp/,   'resize: sharpen == 10');
like($log, qr/JSTEST PASS ir_trans/,   'resize: transparency == true');
like($log, qr/JSTEST PASS ir_inter/,   'resize: interlace == true');
like($log, qr/JSTEST PASS ir_buf/,     'resize: bufferSize == 2097152');
like($log, qr/JSTEST PASS iro_action/, 'rotate: action == "rotate"');
like($log, qr/JSTEST PASS iro_angle/,  'rotate: angle == 90');
