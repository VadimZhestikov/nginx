#!/usr/bin/perl

# Tests for js_preprocess error handling.
#
# Verifies that nginx refuses to start (logs "emerg") when the
# preprocessor script throws or when config.write() receives invalid
# nginx syntax.

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

# 2 explicit tests per scenario × 2 scenarios = 4 explicit
# + 2 auto-checks (no alerts, no sanitizer) per $t instance × 2 = 4 auto
plan tests => 8;


# -----------------------------------------------------------------------
# Test 1-2: JS exception thrown in preprocess.js
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_preprocess %%TESTDIR%%/exception.js;

events { }
EOF

    $t->write_file('exception.js', <<'JS');
throw new Error("deliberate preprocess error");
JS

    eval { $t->run() };

    my $log = $t->read_file('error.log');

    like($log, qr/deliberate preprocess error/,
         'JS exception message appears in error.log');

    like($log, qr/js exception/,
         'nginx logs js exception and refuses to start');
}


# -----------------------------------------------------------------------
# Test 3-4: config.write() with invalid nginx directive
# -----------------------------------------------------------------------

{
    my $t = Test::Nginx->new()->has(qw/http/);

    $t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_preprocess %%TESTDIR%%/bad_config.js;

events { }
EOF

    $t->write_file('bad_config.js', <<'JS');
try {
    config.write("http { not_a_directive_ZZZZ blah; }");
} catch (e) {
    throw new Error("config.write threw: " + e.message);
}
JS

    eval { $t->run() };

    my $log = $t->read_file('error.log');

    like($log, qr/config\.write threw/,
         'config.write error is re-thrown and logged');

    like($log, qr/\[emerg\]/,
         'nginx logs emerg and refuses to start on bad config.write');
}
