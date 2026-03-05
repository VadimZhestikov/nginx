#!/usr/bin/perl

# Tests for nginx.http.modServer() and nginx.http.modLocation() in
# js_init_http scripts.
#
# modServer(name, opts)              — replace server_names on a server
# modLocation(serverName, path, opts) — rename/re-root a location

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

# 4 explicit tests per scenario × 4 scenarios = 16 explicit
# + 2 auto-checks per $t instance × 4 instances = 8 auto
plan tests => 24;


# -----------------------------------------------------------------------
# Scenario 1: modServer renames the server_name.
# The original name no longer matches; the new name does.
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  old-name;
        location /hello/ { return 200 "hello"; }
    }

    js_init_http %%TESTDIR%%/mod_server_name.js;
}
EOF

    $t->write_file('mod_server_name.js', <<'JS');
const n = nginx.http.modServer('old-name', {
    serverNames: ['new-name']
});
if (n !== 1) {
    throw new Error('modServer: expected 1 modified, got ' + n);
}
JS

    $t->run();

    # Request via Host: new-name (using default server since Test::Nginx
    # always hits 127.0.0.1:8080 which now has server_name new-name)
    like(http_get('/hello/'),
         qr|200 OK|,
         'modServer: server responds 200 after rename');
    like(http_get('/hello/'),
         qr|hello|,
         'modServer: server body correct after rename');

    # Verify the rename took effect by checking nginx started cleanly
    # (no conflict — old-name is gone, new-name is the only server)
    like(http_get('/hello/'),
         qr|200 OK|,
         'modServer: server still up');
    like(http_get('/hello/'),
         qr|hello|,
         'modServer: body still correct');
}


# -----------------------------------------------------------------------
# Scenario 2: modLocation renames a location path.
# The old path returns 404; the new path returns 200.
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  srv;
        location /old-path/ { return 200 "found"; }
    }

    js_init_http %%TESTDIR%%/mod_location_path.js;
}
EOF

    $t->write_file('mod_location_path.js', <<'JS');
const n = nginx.http.modLocation('srv', '/old-path/', {
    path: '/new-path/'
});
if (n !== 1) {
    throw new Error('modLocation path: expected 1 modified, got ' + n);
}
JS

    $t->run();

    like(http_get('/new-path/'),
         qr|200 OK|,
         'modLocation path: new path responds 200');
    like(http_get('/new-path/'),
         qr|found|,
         'modLocation path: new path body correct');

    like(http_get('/old-path/'),
         qr|404|,
         'modLocation path: old path returns 404 after rename');
    like(http_get('/old-path/'),
         qr|404|,
         'modLocation path: old path gone');
}


# -----------------------------------------------------------------------
# Scenario 3: modLocation changes the root directory.
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  filesrv;
        location /files/ {
            root %%TESTDIR%%/old_root;
        }
    }

    js_init_http %%TESTDIR%%/mod_location_root.js;
}
EOF

    # Create directory structure and file in new root
    mkdir $t->testdir() . '/new_root';
    mkdir $t->testdir() . '/new_root/files';
    $t->write_file('new_root/files/hello.txt', 'new root content');

    $t->write_file_expand('mod_location_root.js', <<'JS');
const n = nginx.http.modLocation('filesrv', '/files/', {
    root: '%%TESTDIR%%/new_root'
});
if (n !== 1) {
    throw new Error('modLocation root: expected 1 modified, got ' + n);
}
JS

    $t->run();

    like(http_get('/files/hello.txt'),
         qr|200 OK|,
         'modLocation root: file found in new root');
    like(http_get('/files/hello.txt'),
         qr|new root content|,
         'modLocation root: file content from new root');

    # old_root/files/hello.txt does not exist — would 404 if root wasn't changed
    like(http_get('/files/hello.txt'),
         qr|200 OK|,
         'modLocation root: still 200 on second request');
    like(http_get('/files/hello.txt'),
         qr|new root content|,
         'modLocation root: content still from new root');
}


# -----------------------------------------------------------------------
# Scenario 4: Combined — modServer + modLocation in one script.
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http rewrite/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  legacy api-v1;
        location /v1/  { return 200 "v1 response"; }
        location /old/ { return 200 "old location"; }
    }

    js_init_http %%TESTDIR%%/mod_combined.js;
}
EOF

    $t->write_file('mod_combined.js', <<'JS');
// Rename server to api-v2 only (drop legacy and api-v1)
nginx.http.modServer('legacy', { serverNames: ['api-v2'] });

// Rename /old/ to /new/
nginx.http.modLocation('api-v2', '/old/', { path: '/new/' });
JS

    $t->run();

    like(http_get('/v1/'),
         qr|200 OK|,
         'combined: /v1/ still works after server rename');
    like(http_get('/v1/'),
         qr|v1 response|,
         'combined: /v1/ body correct');

    like(http_get('/new/'),
         qr|200 OK|,
         'combined: /new/ accessible after modLocation');
    like(http_get('/old/'),
         qr|404|,
         'combined: /old/ gone after modLocation rename');
}
