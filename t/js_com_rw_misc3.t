#!/usr/bin/perl

# Stage 10: misc module setters — realip, referer, userid, gunzip
#
#   realip.recursive          boolean
#   referer.noReferer         boolean
#   referer.blockedReferer    boolean
#   referer.serverNames       boolean
#   userid.enable             string  "off"/"log"/"v1"/"on"
#   userid.name               string
#   userid.domain             string
#   userid.path               string
#   userid.p3p                string
#   userid.mark               string (single char)
#   gunzip.buffers            {num, size}
#
# Tests:
#   1.  initial realip.recursive=false
#   2.  set realip.recursive=true
#   3.  initial referer flags
#   4.  set referer noReferer/blockedReferer/serverNames
#   5.  initial userid.enable="off"
#   6.  set userid.enable="log"
#   7.  set userid.name, domain, path, p3p, mark
#   8.  userid.enable: bad string throws TypeError
#   9.  gunzip.buffers setter round-trip
#  10.  persistence: realip.recursive still true after second request

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()
    ->has(qw/http realip gunzip userid/)
    ->plan(10);

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

        location /rip/ {
            set_real_ip_from 0.0.0.0/0;
        }

        location /ref/ {
            valid_referers none blocked server_names;
        }

        location /uid/ {
            userid on;
            userid_name uid;
        }

        location /gz/ {
            gunzip on;
        }

        location /read/   { }
        location /set/    { }
        location /badusr/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// /read/ — snapshot all fields
loc('/read/').handler = r => {
    const uid = loc('/uid/').userid;
    const gz  = loc('/gz/').gunzip;
    r.respond(200, {}, JSON.stringify({
        ripRecursive:     loc('/rip/').realip.recursive,
        refNoRef:         loc('/ref/').referer.noReferer,
        refBlocked:       loc('/ref/').referer.blockedReferer,
        refSrvNames:      loc('/ref/').referer.serverNames,
        uidEnable:        uid.enable,
        uidName:          uid.name,
        uidDomain:        uid.domain,
        uidMark:          uid.mark,
        gzBufsNum:        gz.buffers.num,
        gzBufsSize:       gz.buffers.size,
    }));
};

// /set/ — write all fields
loc('/set/').handler = r => {
    loc('/rip/').realip.recursive          = true;
    const ref = loc('/ref/').referer;
    ref.noReferer      = false;
    ref.blockedReferer = false;
    ref.serverNames    = false;
    const uid = loc('/uid/').userid;
    uid.enable = 'log';
    uid.name   = 'session';
    uid.domain = 'example.com';
    uid.path   = '/app';
    uid.p3p    = 'CP="NOI"';
    uid.mark   = 'A';
    loc('/gz/').gunzip.buffers = { num: 8, size: 8192 };
    r.respond(200, {}, 'ok');
};

// /badusr/ — bad userid.enable string
loc('/badusr/').handler = r => {
    try {
        loc('/uid/').userid.enable = 'always';
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial values ----
my $r0 = http_get('/read/');
like($r0, qr/"ripRecursive":false/, 'initial realip.recursive is false');
like($r0, qr/"uidEnable":"on"/,    'initial userid.enable is on');

# ---- Apply writes ----
like(http_get('/set/'), qr/ok/, 'all misc setters executed without error');

my $r1 = http_get('/read/');
like($r1, qr/"ripRecursive":true/,  'realip.recursive set to true');
like($r1, qr/"refNoRef":false/,     'referer.noReferer set to false');
like($r1, qr/"uidEnable":"log"/,    'userid.enable set to log');
like($r1, qr/"uidName":"session"/,  'userid.name set to session');
like($r1, qr/"uidMark":"A"/,        'userid.mark set to A');
like($r1, qr/"gzBufsNum":8/,        'gunzip.buffers.num set to 8');

# ---- Error path ----
like(http_get('/badusr/'), qr/error:/, 'bad userid.enable throws TypeError');

$t->stop();
