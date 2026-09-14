#!/usr/bin/perl

# A confined fragment that returns `undefined` returns UNDEFINED, not a syntax
# error.
#
# The invoke marshals a fragment's result by JSON-stringifying it in the
# compartment and re-parsing it in the host -- only data crosses.  But
# JSON.stringify(undefined) is `undefined`: not the string "undefined", and not
# any JSON text at all.  That value was handed straight to the host's JSON
# parser, which reported
#
#     SyntaxError: unexpected token: 'undefined'   at <result>:1:1
#
# naming neither the fragment nor the cause.
#
# AND `undefined` IS NOT AN EXOTIC RETURN VALUE HERE -- it is what EVERY DENIED
# GATE produces.  A policy whose last statement reads a redacted field, or calls
# an operation its mediation refuses, returns undefined by construction.  So the
# one path an operator is most likely to hit while TIGHTENING a policy was the
# path that reported an internal parse failure against a pseudo-file they had
# never heard of.
#
# It survived this long because every existing probe happened to wrap its result:
# the V12 corpus rows return the string 'denied', the outbound probes return an
# object.  It was found by a `window` probe that returned a denied call directly,
# which is the most natural thing to write.
#
# The same reasoning covers the other values JSON declines to represent -- a
# function, a symbol.  Undefined crossing as undefined is the honest answer when
# there is no data to cross.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;
use JSON::PP;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%
    access_log off;

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /u { }
    }
}
EOF

$t->write_file_expand('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var sock = nginx.createSocket("127.0.0.1:%%PORT_8091%%");

locs.forEach(function (l) {
    if (l.path !== '/u') { return; }
    l.handler = function (req) {
        var o = {};
        function step(name, fn) {
            try { var v = fn(); o[name] = (v === undefined) ? 'undefined' : v; }
            catch (e) { o[name] = 'THREW: ' + String(e && e.message); }
        }

        /* the bare literal */
        step('literal', function () {
            var f = comcon.include("function(a){ return undefined; }",
                                   { imports: [] });
            return f({});
        });

        /* a function with no return at all -- the same value, written the way
         * an operator writes it by accident */
        step('noReturn', function () {
            var f = comcon.include("function(a){ var x = 1; }", { imports: [] });
            return f({});
        });

        /* THE REAL CASE: a denied gate.  `address` is redacted away, so reading
         * it yields undefined, and returning it used to be a syntax error. */
        step('deniedRead', function () {
            var m = comcon.mediate(sock, comcon.redact(['address']));
            var f = comcon.include("function(a){ return s.address; }",
                                   { imports: [], grants: { s: m } });
            return f({});
        });

        /* a value JSON also declines to represent */
        step('func', function () {
            var f = comcon.include("function(a){ return function(){}; }",
                                   { imports: [] });
            return f({});
        });

        /* and the ordinary cases still marshal */
        step('num',  function () {
            return comcon.include("function(a){ return 41 + 1; }", { imports: [] })({});
        });
        step('obj',  function () {
            var v = comcon.include("function(a){ return {a:1,b:'two'}; }",
                                   { imports: [] })({});
            return JSON.stringify(v);
        });
        step('nul',  function () {
            var v = comcon.include("function(a){ return null; }", { imports: [] })({});
            return (v === null) ? 'null' : ('NOT-NULL:' + String(v));
        });

        req.respond(200, { 'content-type': 'application/json' },
                    JSON.stringify(o));
    };
});
JS

$t->try_run('no js module')->plan(7);

my $raw = http_get('/u');
$raw =~ s/^.*?\r\n\r\n//s;
my $o;
eval { $o = decode_json($raw); 1 } or do {
    diag("non-JSON: " . substr($raw, 0, 300)); $o = {};
};

is($o->{literal}, 'undefined',
   'a fragment returning the literal `undefined` yields undefined in the host');
is($o->{noReturn}, 'undefined',
   'and so does a fragment with no return statement -- the same value, written '
   . 'the way it happens by accident');

is($o->{deniedRead}, 'undefined',
   'THE CASE THAT MATTERS: returning a value a mediation redacted away yields '
   . 'undefined, not "SyntaxError: unexpected token" against a pseudo-file the '
   . 'operator has never heard of');

is($o->{func}, 'undefined',
   'a function -- which JSON also declines to represent -- crosses as undefined '
   . 'rather than as a parse failure');

is($o->{num}, 42, 'ordinary values still marshal: a number');
is($o->{obj}, '{"a":1,"b":"two"}', '...an object');
is($o->{nul}, 'null',
   '...and null stays NULL rather than collapsing into undefined, which is the '
   . 'distinction the fix must not erase');

$t->stop();
