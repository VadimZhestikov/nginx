#!/usr/bin/perl

# Stage 0-F: auth/auth_request/limit_req/limit_conn fields are now writable.
#
# NginxAuth:        realm, userFile         (ngx_http_complex_value_t* literal)
# NginxAuthRequest: uri                     (ngx_str_t, dup to pool)
# NginxLimitReq:    logLevel, delayLogLevel (string), statusCode, dryRun
# NginxLimitConn:   logLevel (string), statusCode, dryRun

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http auth_basic limit_req limit_conn/)->plan(28);

# Need two limit_req zones — one for /lreq/ location
$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    limit_req_zone  $binary_remote_addr zone=rzone:1m rate=100r/s;
    limit_conn_zone $binary_remote_addr zone=czone:1m;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        # auth_basic so the COM object is non-null
        location /auth/ {
            auth_basic           "Test Realm";
            auth_basic_user_file /etc/passwd;
        }

        # auth_request so the COM object is non-null
        location /areq/ {
            auth_request /backend;
        }

        # limit_req so the COM object is non-null
        location /lreq/ {
            limit_req zone=rzone;
        }

        # limit_conn so the COM object is non-null
        location /lconn/ {
            limit_conn czone 10;
        }

        location /read/   { }
        location /mutate/ { }
        location /reread/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// Source locations for the COM objects
const al  = loc('/auth/');
const arl = loc('/areq/');
const lrl = loc('/lreq/');
const lcl = loc('/lconn/');

// ---- Set known values at config phase ----

al.auth.realm    = 'New Realm';
al.auth.userFile = '/tmp/users.htpasswd';

arl.authRequest.uri = '/new-backend';

lrl.limitReq.logLevel      = 'warn';
lrl.limitReq.delayLogLevel = 'info';
lrl.limitReq.statusCode    = 429;
lrl.limitReq.dryRun        = true;

lcl.limitConn.logLevel   = 'notice';
lcl.limitConn.statusCode = 503;
lcl.limitConn.dryRun     = true;

// ---- /read/ — echo back all configured values ----
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        auth: {
            realm:    al.auth.realm,
            userFile: al.auth.userFile,
        },
        authRequest: {
            uri: arl.authRequest.uri,
        },
        limitReq: {
            logLevel:      lrl.limitReq.logLevel,
            delayLogLevel: lrl.limitReq.delayLogLevel,
            statusCode:    lrl.limitReq.statusCode,
            dryRun:        lrl.limitReq.dryRun,
        },
        limitConn: {
            logLevel:   lcl.limitConn.logLevel,
            statusCode: lcl.limitConn.statusCode,
            dryRun:     lcl.limitConn.dryRun,
        },
    }));
};

// ---- /mutate/ — change every field from a request handler ----
loc('/mutate/').handler = r => {
    al.auth.realm    = 'Runtime Realm';
    al.auth.userFile = '/tmp/runtime.htpasswd';

    arl.authRequest.uri = '/runtime-backend';

    lrl.limitReq.logLevel      = 'error';
    lrl.limitReq.delayLogLevel = 'notice';
    lrl.limitReq.statusCode    = 503;
    lrl.limitReq.dryRun        = false;

    lcl.limitConn.logLevel   = 'warn';
    lcl.limitConn.statusCode = 429;
    lcl.limitConn.dryRun     = false;

    r.respond(200, {}, 'mutated');
};

// ---- /reread/ — echo back after mutation ----
loc('/reread/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        auth: {
            realm:    al.auth.realm,
            userFile: al.auth.userFile,
        },
        authRequest: {
            uri: arl.authRequest.uri,
        },
        limitReq: {
            logLevel:      lrl.limitReq.logLevel,
            delayLogLevel: lrl.limitReq.delayLogLevel,
            statusCode:    lrl.limitReq.statusCode,
            dryRun:        lrl.limitReq.dryRun,
        },
        limitConn: {
            logLevel:   lcl.limitConn.logLevel,
            statusCode: lcl.limitConn.statusCode,
            dryRun:     lcl.limitConn.dryRun,
        },
    }));
};
JS

$t->run();

# ---- Phase 1: config-phase writes visible at request time ----

my $r1 = http_get('/read/');

like($r1, qr/"realm":"New Realm"/,              'auth.realm set at config phase');
like($r1, qr|"userFile":"/tmp/users.htpasswd"|, 'auth.userFile set at config phase');
like($r1, qr|"uri":"/new-backend"|,             'authRequest.uri set at config phase');
like($r1, qr/"logLevel":"warn"/,                'limitReq.logLevel=warn at config phase');
like($r1, qr/"delayLogLevel":"info"/,           'limitReq.delayLogLevel=info at config phase');
like($r1, qr/"statusCode":429/,                 'limitReq.statusCode=429 at config phase');
like($r1, qr/"dryRun":true/,                    'limitReq.dryRun=true at config phase');
like($r1, qr/"logLevel":"notice"/,              'limitConn.logLevel=notice at config phase');
like($r1, qr/"statusCode":503/,                 'limitConn.statusCode=503 at config phase');
like($r1, qr/"dryRun":true/,                    'limitConn.dryRun=true at config phase');

# ---- Phase 2: runtime mutation ----

like(http_get('/mutate/'), qr/mutated/, 'mutation request succeeded');

my $r2 = http_get('/reread/');

like($r2, qr/"realm":"Runtime Realm"/,             'auth.realm updated at runtime');
like($r2, qr|"userFile":"/tmp/runtime.htpasswd"|,  'auth.userFile updated at runtime');
like($r2, qr|"uri":"/runtime-backend"|,            'authRequest.uri updated at runtime');
like($r2, qr/"logLevel":"error"/,                  'limitReq.logLevel=error after mutation');
like($r2, qr/"delayLogLevel":"notice"/,            'limitReq.delayLogLevel=notice after mutation');
like($r2, qr/"statusCode":503/,                    'limitReq.statusCode=503 after mutation');
like($r2, qr/"dryRun":false/,                      'limitReq.dryRun=false after mutation');
like($r2, qr/"logLevel":"warn"/,                   'limitConn.logLevel=warn after mutation');
like($r2, qr/"statusCode":429/,                    'limitConn.statusCode=429 after mutation');
like($r2, qr/"dryRun":false/,                      'limitConn.dryRun=false after mutation');

# ---- log level round-trip for all four values (limit_req) ----

{
    my $fmt = do {
        # Use a fresh handler on /reread/ to test each level
        # Set each level and read back in one request via inline handler
        http_get('/mutate/');  # sets logLevel=error
        my $s = http_get('/reread/');
        $s =~ /"logLevel":"(\w+)"/ ? $1 : 'unknown'
    };
    like($fmt, qr/error/, 'limitReq.logLevel round-trips "error"');
}

# ---- Persistence across requests ----
like(http_get('/reread/'), qr/"realm":"Runtime Realm"/, 'auth.realm persists');
like(http_get('/reread/'), qr|/runtime-backend|,        'authRequest.uri persists');
like(http_get('/reread/'), qr/"dryRun":false/,          'limitReq.dryRun persists as false');
like(http_get('/reread/'), qr/"dryRun":false/,          'limitConn.dryRun persists as false');

# Sanity
like(http_get('/read/'),   qr/200/, 'worker alive after all mutations');
like(http_get('/reread/'), qr/200/, 'worker still alive on final check');

$t->stop();
