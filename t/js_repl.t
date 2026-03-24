#!/usr/bin/perl

# Tests for the nginx JS REPL:
#   nginx.repl.eval(line)       → {status, value, message, stack}
#   nginx.repl._writeFd(fd,str) → synchronous write
#   nginx.repl.attach(fd,…)     → console/nginx.log streaming
#   nginx.repl.detach()         → restore originals
#   req.hijack()                → raw fd for the connection
#   nginx.repl.listen(fd,fn)    → line-by-line read handler
#   nginx.workerIdx             → 0-based worker index

use warnings;
use strict;
use Test::More;
use IO::Socket::INET;
use POSIX qw();

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/repl_init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen 127.0.0.1:8080;
        server_name localhost;

        # eval-only endpoint: evaluates ?line= and returns JSON result
        location /eval/  { }

        # workerIdx endpoint: returns nginx.workerIdx
        location /widx/  { }

        # full REPL endpoint: hijacks connection, runs line-based protocol
        location /repl/  { }
    }
}
EOF

$t->write_file('repl_init.js', <<'JS');

(function setup() {
    const locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    /* /eval/ — evaluate the "line" query param and respond with JSON */
    set('/eval/', function(req) {
        const line = req.queryParams.line || '';
        const r = nginx.repl.eval(line);
        req.json(r);
    });

    /* /widx/ — return nginx.workerIdx as plain text */
    set('/widx/', function(req) {
        req.text(String(nginx.workerIdx));
    });

    /* /repl/ — hijack, send ready marker, run line-based REPL protocol */
    set('/repl/', function(req) {
        const fd = req.hijack();

        /* Acknowledge the HTTP request with a minimal 101 response
         * so the client knows the connection is now in raw mode.     */
        nginx.repl._writeFd(fd,
            'HTTP/1.1 101 Switching Protocols\r\n' +
            'Connection: Upgrade\r\n' +
            'Upgrade: nginx-repl\r\n' +
            '\r\n');

        /* Optionally attach console logging at level 2 (log+warn+error),
         * nginx.log at level 6 (info).                                */
        const loglevel   = parseInt(req.queryParams.loglevel  || '2');
        const nginxlevel = parseInt(req.queryParams.nginxlevel || '6');
        nginx.repl.attach(fd, loglevel, nginxlevel);

        nginx.repl.listen(fd, function(line) {
            // parse: "<token> <CMD> [<payload>]"
            const sp1   = line.indexOf(' ');
            if (sp1 < 0) { return; }
            const token = line.slice(0, sp1);
            const rest  = line.slice(sp1 + 1);
            const sp2   = rest.indexOf(' ');
            const cmd   = sp2 < 0 ? rest : rest.slice(0, sp2);
            const arg   = sp2 < 0 ? ''   : rest.slice(sp2 + 1);

            if (cmd === 'EVAL') {
                const r = nginx.repl.eval(arg);
                if (r.status === 'ok') {
                    const val = (r.value !== undefined && r.value !== null)
                                ? String(r.value) : '';
                    nginx.repl._writeFd(fd, token + ' OK ' + val + '\n');
                } else if (r.status === 'incomplete') {
                    nginx.repl._writeFd(fd, token + ' INCOMPLETE\n');
                } else {
                    nginx.repl._writeFd(fd, token + ' ERR ' + r.message + '\n');
                    if (r.stack) {
                        const lines = String(r.stack).split('\n');
                        for (var i = 0; i < lines.length; i++) {
                            if (lines[i]) {
                                nginx.repl._writeFd(fd,
                                    token + ' STACK ' + lines[i] + '\n');
                            }
                        }
                        nginx.repl._writeFd(fd, token + ' STACK_END\n');
                    }
                }
            } else if (cmd === 'DETACH') {
                nginx.repl.detach();
                nginx.repl._writeFd(fd, token + ' OK\n');
            } else if (cmd === 'LOG') {
                /* trigger a console.log — used to test streaming */
                console.log(arg);
                nginx.repl._writeFd(fd, token + ' OK\n');
            } else if (cmd === 'NGINXLOG') {
                /* trigger nginx.log — used to test nginx.log streaming */
                nginx.log(parseInt(arg.split(' ')[0]), arg.split(' ').slice(1).join(' '));
                nginx.repl._writeFd(fd, token + ' OK\n');
            }
        });
    });
})();
JS

$t->try_run('no js module')->plan(14);

# ------------------------------------------------------------------ #
# Helpers                                                              #
# ------------------------------------------------------------------ #

sub http_get_path {
    my ($path) = @_;
    return http("GET $path HTTP/1.0\r\nHost: localhost\r\n\r\n");
}

# Open a raw TCP socket, send the HTTP upgrade, read the 101 response,
# return the connected IO::Socket.
sub repl_connect {
    my ($extra_qs) = @_;
    $extra_qs //= '';

    my $sock = IO::Socket::INET->new(
        PeerAddr => '127.0.0.1',
        PeerPort => port(8080),
        Proto    => 'tcp',
        Timeout  => 5,
    ) or die "connect: $!";

    my $qs = 'loglevel=2&nginxlevel=6' . ($extra_qs ? "&$extra_qs" : '');
    print $sock "GET /repl/?$qs HTTP/1.1\r\nHost: localhost\r\nConnection: upgrade\r\nUpgrade: nginx-repl\r\n\r\n";

    # Read until end of headers
    my $buf = '';
    while (1) {
        my $line = <$sock>;
        last unless defined $line;
        $line =~ s/\r\n$//;
        last if $line eq '';
        $buf .= $line . "\n";
    }

    die "upgrade failed: $buf" unless $buf =~ /101/;
    return $sock;
}

# Send one command and return raw lines until we see the token response.
# Collects LOG lines separately.
sub repl_cmd {
    my ($sock, $cmd) = @_;
    my $token = 't' . int(rand(9000) + 1000);
    print $sock "$token $cmd\n";

    my @log_lines;
    my $reply = '';
    my @stack;
    my $in_stack = 0;

    while (1) {
        my $line = <$sock>;
        last unless defined $line;
        $line =~ s/\r?\n$//;

        if ($line =~ /^\* LOG /) {
            push @log_lines, $line;
            next;
        }

        next unless $line =~ /^\Q$token\E /;

        if ($line =~ /^\Q$token\E STACK (.*)$/) {
            push @stack, $1;
            next;
        }
        if ($line =~ /^\Q$token\E STACK_END$/) {
            next;
        }

        $reply = $line;
        last;
    }

    return { raw => $reply, log => \@log_lines, stack => \@stack };
}

# ------------------------------------------------------------------ #
# Test 1: nginx.workerIdx is 0 (single worker, first worker)          #
# ------------------------------------------------------------------ #

like(http_get_path('/widx/'), qr/^0$/m, 'nginx.workerIdx is 0 for first worker');

# ------------------------------------------------------------------ #
# Tests 2–5: nginx.repl.eval via /eval/ HTTP endpoint                 #
# ------------------------------------------------------------------ #

like(
    http_get_path('/eval/?line=1+2'),
    qr/"status":"ok"/,
    'eval: expression ok status'
);

like(
    http_get_path('/eval/?line=1+2'),
    qr/"value":"3"/,
    'eval: expression result value is 3'
);

like(
    http_get_path('/eval/?line=undefined_var_xyz'),
    qr/"status":"error"/,
    'eval: reference error returns status error'
);

like(
    http_get_path('/eval/?line=%5B1%2C'),
    qr/"status":"incomplete"/,
    'eval: incomplete expression returns incomplete'
);

# ------------------------------------------------------------------ #
# Tests 6–8: raw REPL connection via req.hijack()                     #
# ------------------------------------------------------------------ #

my $sock = repl_connect();

my $r = repl_cmd($sock, 'EVAL 6 * 7');
like($r->{raw}, qr/OK 42/, 'repl: eval 6*7 returns 42');

$r = repl_cmd($sock, 'EVAL "hello".toUpperCase()');
like($r->{raw}, qr/OK "HELLO"/, 'repl: eval string expression');

$r = repl_cmd($sock, 'EVAL throw new Error("boom")');
like($r->{raw}, qr/ERR.*boom/, 'repl: thrown error returned as ERR');

# ------------------------------------------------------------------ #
# Test 9: incomplete multiline input                                   #
# ------------------------------------------------------------------ #

$r = repl_cmd($sock, 'EVAL function f(');
like($r->{raw}, qr/INCOMPLETE/, 'repl: incomplete function decl returns INCOMPLETE');

# ------------------------------------------------------------------ #
# Tests 10–11: console.log streaming via attach                        #
# ------------------------------------------------------------------ #

$r = repl_cmd($sock, 'LOG hello from console');
my @logs = grep { /LOG console:log hello from console/ } @{$r->{log}};
ok(scalar(@logs) > 0, 'repl: console.log line received as LOG message');

# console.debug should NOT appear (minLevel=2, debug is level 1 > 2)
$r = repl_cmd($sock, 'EVAL console.debug("silent")');
@logs = grep { /LOG console:debug/ } @{$r->{log}};
ok(scalar(@logs) == 0, 'repl: console.debug suppressed by loglevel=2');

# ------------------------------------------------------------------ #
# Test 12: nginx.log streaming                                         #
# ------------------------------------------------------------------ #

# nginx level 4 (warn) — nginxlevel=6 means levels 1–6 forwarded
$r = repl_cmd($sock, 'NGINXLOG 4 test-warn-message');
@logs = grep { /LOG nginx:4.*test-warn-message/ } @{$r->{log}};
ok(scalar(@logs) > 0, 'repl: nginx.log forwarded as LOG nginx:4');

# ------------------------------------------------------------------ #
# Test 13: nginx.log level 8 (debug) above nginxlevel=6 suppressed    #
# ------------------------------------------------------------------ #

$r = repl_cmd($sock, 'NGINXLOG 8 silent-debug');
@logs = grep { /LOG nginx:8.*silent-debug/ } @{$r->{log}};
ok(scalar(@logs) == 0, 'repl: nginx.log debug suppressed by nginxlevel=6');

# ------------------------------------------------------------------ #
# Test 14: detach restores nginx.log (no forwarding after detach)      #
# ------------------------------------------------------------------ #

repl_cmd($sock, 'DETACH');
$r = repl_cmd($sock, 'NGINXLOG 4 after-detach');
@logs = grep { /LOG nginx:4.*after-detach/ } @{$r->{log}};
ok(scalar(@logs) == 0, 'repl: nginx.log not forwarded after detach');

close $sock;
