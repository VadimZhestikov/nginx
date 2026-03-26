#!/usr/bin/perl

# Tests for nginx.on() master supervisory-loop lifecycle hooks.
#
# Verifies that JS callbacks registered via nginx.on() fire at the
# expected points in the master process lifecycle:
#   workerSpawned  — initial worker start
#   reopen         — SIGUSR1
#   reload/reloaded — SIGHUP (old ctx / new ctx)
#   quit           — SIGQUIT
#   workerExited   — after workers die on SIGQUIT

use warnings;
use strict;
use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(7);
my $dir = $t->testdir();

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/master_events.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:%%PORT_8080%%;
        server_name  localhost;

        location / {
            return 200 "ok\n";
        }
    }
}
EOF

$t->write_file_expand('master_events.js', <<'JS');
import * as std from 'std';

var evlog = '%%TESTDIR%%/events.log';

function append(msg) {
    var f = std.open(evlog, 'a');
    if (f) { f.puts(msg + '\n'); f.close(); }
}

nginx.on('workerSpawned', function(pid, slot) {
    append('workerSpawned:' + pid + ':' + slot);
});

nginx.on('workerExited', function(pid, slot, status) {
    append('workerExited:' + pid + ':' + slot);
});

nginx.on('quit',     function() { append('quit');     });
nginx.on('reload',   function() { append('reload');   });
nginx.on('reloaded', function() { append('reloaded'); });
nginx.on('reopen',   function() { append('reopen');   });
JS

$t->run();
select undef, undef, undef, 0.3;   # let workers start

sub evlog_read {
    my $path = "$dir/events.log";
    return '' unless -f $path;
    open my $fh, '<', $path or return '';
    local $/;
    return <$fh> // '';
}

# --- workerSpawned fires when workers start ---
like(evlog_read(), qr/workerSpawned:\d+:\d+/, 'workerSpawned on startup');

# --- HTTP server is live ---
like(http_get('/'), qr/200/, 'HTTP works after startup');

# --- reopen (SIGUSR1) ---
kill 'USR1', $t->read_file('nginx.pid');
select undef, undef, undef, 0.2;
like(evlog_read(), qr/reopen/, 'reopen fired on SIGUSR1');

# --- reload (SIGHUP): both reload and reloaded should appear ---
$t->reload();
select undef, undef, undef, 0.5;
my $log = evlog_read();
like($log, qr/reload/,   'reload fired on SIGHUP');
like($log, qr/reloaded/, 'reloaded fired after SIGHUP');

# --- quit (SIGQUIT): stop nginx, then check the log ---
$t->stop();
$log = evlog_read();
like($log, qr/quit/,              'quit fired on SIGQUIT');
like($log, qr/workerExited:\d+:\d+/, 'workerExited fired after SIGQUIT');
