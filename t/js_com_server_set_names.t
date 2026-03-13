#!/usr/bin/perl

# Stage 5a: server.setNames(arr)
#
# setNames(arr) replaces op->names/nnames (the COM-visible copy) and
# rebuilds cscf->server_names in ngx_cycle->pool for Stage 5b.
#
# Tests:
#   1. names getter returns initial server_name from config
#   2. name getter (first entry) returns initial server_name
#   3. setNames(["new.example.com"]) succeeds
#   4. names getter shows new array immediately
#   5. name getter (first entry) shows new first name
#   6. Persistence: second /read/ still shows new names
#   7. setNames(["a.example", "b.example"]) — multiple names
#   8. names[0] and names[1] both present
#   9. setNames([]) — empty array succeeds
#  10. names getter returns empty array after setNames([])
#  11. Error path: non-array argument throws TypeError

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(11);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name original.example.com;

        location /read/   { }
        location /set1/   { }
        location /set2/   { }
        location /empty/  { }
        location /badarg/ { }
    }
}
EOF

$t->write_file('init.js', <<'JS');
const srv = nginx.http.servers[0];

function loc(path) {
    return srv.locations.find(l => l.path === path);
}

// ---- /read/ — return names array and first name ----
loc('/read/').handler = r => {
    r.respond(200, {}, JSON.stringify({
        name:  srv.name,
        names: srv.names,
    }));
};

// ---- /set1/ — set a single new name ----
loc('/set1/').handler = r => {
    srv.setNames(['new.example.com']);
    r.respond(200, {}, JSON.stringify({
        name:  srv.name,
        names: srv.names,
    }));
};

// ---- /set2/ — set two names ----
loc('/set2/').handler = r => {
    srv.setNames(['a.example', 'b.example']);
    r.respond(200, {}, JSON.stringify({
        name:  srv.name,
        names: srv.names,
    }));
};

// ---- /empty/ — clear all names ----
loc('/empty/').handler = r => {
    srv.setNames([]);
    r.respond(200, {}, JSON.stringify({
        name:  srv.name,
        names: srv.names,
    }));
};

// ---- /badarg/ — non-array should throw TypeError ----
loc('/badarg/').handler = r => {
    try {
        srv.setNames('not-an-array');
        r.respond(200, {}, 'no-error');
    } catch (e) {
        r.respond(200, {}, 'error:' + e.message);
    }
};
JS

$t->run();

# ---- Initial names from config ----
my $r0 = http_get('/read/');
like($r0, qr/original\.example\.com/, 'names getter returns initial server_name');
like($r0, qr/"name":"original\.example\.com"/, 'name getter returns initial server_name');

# ---- setNames(["new.example.com"]) ----
my $r1 = http_get('/set1/');
like($r1, qr/200/,                'setNames() succeeds without exception');
like($r1, qr/new\.example\.com/,  'names getter updated immediately');
like($r1, qr/"name":"new\.example\.com"/, 'name getter returns new first entry');

# ---- Persistence ----
like(http_get('/read/'), qr/new\.example\.com/, 'new names persist on next request');

# ---- setNames(["a.example","b.example"]) ----
my $r2 = http_get('/set2/');
like($r2, qr/a\.example/, 'first name present after setNames with two names');
like($r2, qr/b\.example/, 'second name present after setNames with two names');

# ---- setNames([]) ----
my $r3 = http_get('/empty/');
like($r3, qr/200/,    'setNames([]) succeeds');
like($r3, qr/\[\]/,   'names getter returns empty array after setNames([])');

# ---- Error path: non-array ----
like(http_get('/badarg/'), qr/error:/, 'setNames with non-array throws TypeError');

$t->stop();
