#!/usr/bin/perl

# Tests for Stage 20 COM expansion: r.subrequest(uri) → Promise<{status,body}>
#
# Issues an nginx internal subrequest (in-memory) from a JS async handler.
# The returned Promise resolves to {status: number, body: string}.

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

js_source %%TESTDIR%%/init_subrequest.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen      127.0.0.1:8080;
        server_name localhost;

        # JS handler that issues a subrequest and echoes its result
        location /proxy {
        }

        # target for the subrequest — static response via return
        location /inner {
            return 200 "hello from inner";
        }

        # target that returns a non-200 status
        location /not_found {
            return 404 "not here";
        }
    }
}
EOF

$t->write_file('init_subrequest.js', <<'JS');
(function() {
    const locs = nginx.http.servers[0].locations;
    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }
    set('/proxy', proxyHandler);
})();

async function proxyHandler(r) {
    const target = r.variable("arg_target") || "/inner";
    const res = await r.subrequest(target);
    r.respond(res.status,
              {"content-type": "text/plain"},
              res.body);
}
JS

$t->try_run('no js module')->plan(6);

# subrequest to /inner — expects 200 + echoed body
my $res = http_get('/proxy?target=/inner');
like($res, qr/200 OK/,          'subrequest /inner: status 200');
like($res, qr/hello from inner/, 'subrequest /inner: body echoed');

# subrequest to /not_found — expects 404 propagated
my $r2 = http_get('/proxy?target=/not_found');
like($r2, qr/404/,    'subrequest /not_found: status 404 propagated');
like($r2, qr/not here/, 'subrequest /not_found: body echoed');

# second call to /inner (verifies no state corruption)
my $r3 = http_get('/proxy?target=/inner');
like($r3, qr/200 OK/,           'second subrequest: status 200');
like($r3, qr/hello from inner/, 'second subrequest: body correct');
