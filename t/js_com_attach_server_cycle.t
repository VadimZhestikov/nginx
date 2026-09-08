#!/usr/bin/perl
# Config-phase attach() -> serverByName() must not bind the OLD cycle.
#
# Regression test for a use-after-free proved with ASAN:
#
#   ERROR: AddressSanitizer: heap-use-after-free, READ of size 8
#     #0 ngx_js_server_get src/js/ngx_js_com_http.c:6826   (op->names[0].data)
#
# The socket-entry serverByName (nginx.cycle.sockets[i]) is a different, clean
# function: a lookup in a pre-built name map, no cycle involved. The one that
# takes a cycle is on the LISTENER prototype, reachable only via attach().
#
# Config phase:  sock -> http.attach() -> addServer() -> serverByName()
#                  => ngx_js_wrap_server(cscf, ngx_cycle = OLD cycle)
#                  => op->cycle = old, op->names from old_cycle->pool
# Request phase: read srv.name (op->names[0]) after ngx_init_cycle destroyed
#                that pool at ngx_cycle.c:783.
#
# NOTE ON WHAT THIS TEST CAN AND CANNOT DO. Natively it passes either way:
# freed pool memory stays mapped, so the bad read returns the right bytes by
# luck. It is a behavioural pin and a path-exerciser. The PROOF is running it
# against an ASAN build, which reports the UAF with the fix reverted and is
# clean with it in place. Do not read a green run here as proof of memory
# safety -- rebuild with -fsanitize=address for that.

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
js_source %%TESTDIR%%/init.js;
events { }
http {
    %%TEST_GLOBALS_HTTP%%
    server {
        listen       127.0.0.1:8080;
        server_name  localhost;
        location /probe { }
    }
}
EOF

$t->write_file_expand('init.js', <<'JS');
var stashed = null, step = "start";

try {
    var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");
    step = "socket";

    var lis = nginx.http.attach(sock);
    step = "attached";

    /* Give the listener a server so serverByName has something to resolve
     * AND so cscf->server_names.nelts > 0, which is what gates the
     * op->names = ngx_palloc(cycle->pool, ...) copy in ngx_js_wrap_server. */
    lis.addServer(nginx.http.servers[0]);
    step = "addServer";

    stashed = lis.serverByName("localhost");
    step = "serverByName:" + (stashed ? "hit" : "miss");
} catch (e) {
    step = "threw@" + step + ":" + e;
}

nginx.log(6, "SBN step=" + step);

var locs = nginx.http.servers[0].locations;
for (var i = 0; i < locs.length; i++) {
    if (locs[i].path === "/probe") {
        locs[i].handler = function (r) {
            var out = { step: step, had: !!stashed };
            if (stashed) {
                /* Reads op->names[0].data — old pool, destroyed long ago. */
                try { out.name = stashed.name; }
                catch (e) { out.nameErr = String(e); }
                /* op->cycle->pool via rebuild_loc_tree. */
                try { stashed.addLocation('/sbn-' + Date.now()); out.added = true; }
                catch (e) { out.addErr = String(e); }
            }
            r.respond(200, {"Content-Type":"application/json"},
                      JSON.stringify(out));
        };
    }
}
JS

# run(), not try_run(): a fault here kills the worker at config phase, and
# try_run() would turn that into a skip that prove counts as success.
$t->run()->plan(5);

my $log = $t->read_file('error.log');
like($log, qr/SBN step=serverByName:hit/,
     'config phase: createSocket -> attach -> addServer -> serverByName resolves');

my $r = http_get('/probe');
like($r, qr/"had":true/,
     'the server handle survives into the worker');
like($r, qr/"name":"localhost"/,
     'reading its name at request time yields the right value (op->names)');
like($r, qr/"added":true/,
     'mutating through it works (op->cycle -> rebuild_loc_tree)');
unlike($r, qr/"nameErr"|"addErr"/,
     'no error from either the read or the mutation');
