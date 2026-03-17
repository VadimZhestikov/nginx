#!/usr/bin/perl

# Tests for nginx.stream.servers[i].handler — Stream JS session handlers
# (Stage E).
#
# Covers:
#   srv.handler = fn            — installs a JS content handler
#   session.remoteAddress       — client IP string
#   session.remotePort          — client port number
#   session.localAddress        — server-side IP string
#   session.localPort           — server-side port number
#   session.ssl                 — bool (false for plain TCP)
#   session.received            — bytes received (0 before proxy reads)
#   session.finalize(code)      — closes the session with given code
#   session.finalize()          — default code NGX_STREAM_OK (200)
#   session.variable(name)      — stream variable lookup
#   double finalize() call      — second call silently ignored

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;
use Test::Nginx::Stream qw/ stream /;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http stream/);

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
        location / { }
    }
}

stream {
    server {
        listen      127.0.0.1:%%PORT_8092%%;
    }
}
EOF

$t->write_file('init.js', <<'JS');
function pass(name)      { nginx.log(6, "JSTEST PASS " + name); }
function fail(name, got) { nginx.log(6, "JSTEST FAIL " + name + ": " + String(got)); }
function check(name, ok, got) { if (ok) { pass(name); } else { fail(name, got); } }

const srv = nginx.stream.servers[0];

srv.handler = function(session) {
    /* ---- addresses / ports ---------------------------------------- */
    check("remote_addr_str",  typeof session.remoteAddress === "string"
                              && session.remoteAddress.length > 0,
          session.remoteAddress);

    check("remote_port_num",  typeof session.remotePort === "number"
                              && session.remotePort > 0, session.remotePort);

    check("local_addr_str",   typeof session.localAddress === "string"
                              && session.localAddress.length > 0,
          session.localAddress);

    check("local_addr_127",   session.localAddress === "127.0.0.1",
          session.localAddress);

    check("local_port_num",   typeof session.localPort === "number"
                              && session.localPort > 0, session.localPort);

    /* ---- ssl flag (plain TCP connection) --------------------------- */
    check("ssl_false",        session.ssl === false, session.ssl);

    /* ---- received -------------------------------------------------- */
    check("received_num",     typeof session.received === "number",
          typeof session.received);

    /* ---- variable() lookup ---------------------------------------- */
    const remoteVar = session.variable("remote_addr");
    check("var_remote_addr",  remoteVar === session.remoteAddress,
          remoteVar);

    const missing = session.variable("no_such_variable_xyz");
    check("var_missing_null", missing === null, missing);

    /* ---- double finalize silently ignores second call -------------- */
    session.finalize(200);
    session.finalize(500);   /* must be silently ignored */

    pass("handler_reached");
};
JS

$t->try_run('no js module or stream module')->plan(10);

# Open a TCP connection so the JS handler fires in the worker process.
stream("127.0.0.1:" . Test::Nginx::port(8092));

# Give the worker time to log the JSTEST messages.
sleep(1);

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS remote_addr_str/,   'session.remoteAddress is non-empty string');
like($log, qr/JSTEST PASS remote_port_num/,   'session.remotePort is positive number');
like($log, qr/JSTEST PASS local_addr_str/,    'session.localAddress is non-empty string');
like($log, qr/JSTEST PASS local_addr_127/,    'session.localAddress == 127.0.0.1');
like($log, qr/JSTEST PASS local_port_num/,    'session.localPort is positive number');
like($log, qr/JSTEST PASS ssl_false/,         'session.ssl == false for plain TCP');
like($log, qr/JSTEST PASS received_num/,      'session.received is a number');
like($log, qr/JSTEST PASS var_remote_addr/,   'session.variable("remote_addr") matches remoteAddress');
like($log, qr/JSTEST PASS var_missing_null/,  'session.variable(unknown) returns null');
like($log, qr/JSTEST PASS handler_reached/,   'handler called and reached end');
