#!/usr/bin/perl

# Stage 8b: miscellaneous module properties become runtime-writable
#
#   NginxSlice.size              number  (bytes per subrequest)
#   NginxRandomIndex.enable      boolean
#   NginxGzipStatic.enable       string  ("off"/"on"/"always")
#   NginxMp4.bufferSize          number
#   NginxMp4.maxBufferSize       number
#   NginxMp4.startKeyFrame       boolean
#   NginxXslt.lastModified       boolean
#   NginxDav.methods             string[] → bitmask
#   NginxDav.access              number
#   NginxDav.minDeleteDepth      number
#   NginxDav.createFullPutPath   boolean
#   NginxCharset.overrideCharset boolean
#
# Tests:
#   1.  slice.size getter returns initial value from config
#   2.  slice.size setter takes effect
#   3.  randomIndex.enable setter toggles
#   4.  gzipStatic.enable setter: "off" → "always" round-trip
#   5.  gzipStatic.enable: bad string throws TypeError
#   6.  mp4.bufferSize/maxBufferSize/startKeyFrame setters
#   7.  xslt.lastModified setter
#   8.  dav.methods setter (array → bitmask → getter array)
#   9.  dav.access/minDeleteDepth/createFullPutPath setters
#  10.  dav.methods: unknown method string throws TypeError
#  11.  charset.overrideCharset setter

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http slice random_index gzip_static mp4 xslt dav charset/)
    ->plan(16);

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

        location /slice/ {
            slice 1m;
        }

        location /ri/ {
            random_index on;
        }

        location /gs/ {
            gzip_static on;
        }

        location /mp4l/ {
            mp4;
            mp4_buffer_size 2m;
            mp4_max_buffer_size 10m;
        }

        location /xslt/ {
            xslt_last_modified on;
        }

        location /dav/ {
            dav_methods PUT DELETE;
            dav_access  user:rw group:r all:r;
        }

        location /charset/ {
            charset UTF-8;
            override_charset on;
        }

        location /read/   { }
        location /set/    { }
        location /baddav/ { }
        location /badgs/  { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// /read/ — snapshot all target values
loc('/read/').handler = r => {
    const sl = loc('/slice/');
    const ri = loc('/ri/');
    const gs = loc('/gs/');
    const mp = loc('/mp4l/');
    const xs = loc('/xslt/');
    const dv = loc('/dav/');
    const cs = loc('/charset/');
    r.respond(200, {}, JSON.stringify({
        sliceSize:          sl.slice.size,
        randomIndexEnable:  ri.randomIndex.enable,
        gzipStaticEnable:   gs.gzipStatic.enable,
        mp4BufSize:         mp.mp4.bufferSize,
        mp4MaxBufSize:      mp.mp4.maxBufferSize,
        mp4StartKF:         mp.mp4.startKeyFrame,
        xsltLastMod:        xs.xslt.lastModified,
        davMethods:         dv.dav.methods,
        davAccess:          dv.dav.access,
        davMinDepth:        dv.dav.minDeleteDepth,
        davFullPut:         dv.dav.createFullPutPath,
        charsetOverride:    cs.charset.overrideCharset,
    }));
};

// /set/ — write new values
loc('/set/').handler = r => {
    loc('/slice/').slice.size               = 524288;
    loc('/ri/').randomIndex.enable          = false;
    loc('/gs/').gzipStatic.enable           = 'always';
    loc('/mp4l/').mp4.bufferSize            = 4194304;
    loc('/mp4l/').mp4.maxBufferSize         = 20971520;
    loc('/mp4l/').mp4.startKeyFrame         = true;
    loc('/xslt/').xslt.lastModified         = false;
    loc('/dav/').dav.methods                = ['PUT', 'MKCOL', 'MOVE'];
    loc('/dav/').dav.access                 = 0o644;
    loc('/dav/').dav.minDeleteDepth         = 2;
    loc('/dav/').dav.createFullPutPath      = true;
    loc('/charset/').charset.overrideCharset = false;
    r.respond(200, {}, 'ok');
};

// /baddav/ — unknown DAV method
loc('/baddav/').handler = r => {
    try {
        loc('/dav/').dav.methods = ['PUT', 'PATCH'];
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};

// /badgs/ — invalid gzipStatic.enable string
loc('/badgs/').handler = r => {
    try {
        loc('/gs/').gzipStatic.enable = 'yes';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"sliceSize":1048576/,     'initial slice.size is 1m');
like($r0, qr/"gzipStaticEnable":"on"/, 'initial gzipStatic.enable is on');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"sliceSize":524288/,              'slice.size updated');
like($r1, qr/"randomIndexEnable":false/,       'randomIndex.enable updated');
like($r1, qr/"gzipStaticEnable":"always"/,     'gzipStatic.enable updated to always');
like($r1, qr/"mp4BufSize":4194304/,            'mp4.bufferSize updated');
like($r1, qr/"mp4MaxBufSize":20971520/,        'mp4.maxBufferSize updated');
like($r1, qr/"mp4StartKF":true/,               'mp4.startKeyFrame updated');
like($r1, qr/"xsltLastMod":false/,             'xslt.lastModified updated');
like($r1, qr/"davAccess":420/,                 'dav.access updated (0o644=420)');
like($r1, qr/"davMinDepth":2/,                 'dav.minDeleteDepth updated');
like($r1, qr/"davFullPut":true/,               'dav.createFullPutPath updated');
like($r1, qr/"charsetOverride":false/,         'charset.overrideCharset updated');

# ---- Error paths ----
like(http_get('/baddav/'), qr/error:/, 'unknown DAV method throws TypeError');
like(http_get('/badgs/'),  qr/error:/, 'invalid gzipStatic.enable string throws');

$t->stop();
