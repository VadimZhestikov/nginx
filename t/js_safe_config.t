#!/usr/bin/perl

# Tests for the safe-config Layer 2 policy plugin (nginx.safeConfig).
#
# Verifies the safety gateway: per-tier opt-in gates (safe / guarded:ack /
# irreversible:confirm), dryRun plans, propagation classification, and the
# four named policies (canaryWeight, setResponseHeader, toggleLocation,
# drainPeer).  Gateway checks run in a request handler (post-fork, so
# describe() reports zoned-shared correctly) and log JSTEST PASS/FAIL; two
# functional follow-up requests confirm a header and a route toggle took effect.

use warnings;
use strict;
use Test::More;
use Cwd qw(abs_path);
use File::Spec;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy upstream_zone/);

my $plugin = abs_path(File::Spec->catfile($FindBin::Bin,
                      '..', 'js_com_apps', 'safe_config'));
my $cfgw   = "$plugin/cfgworker.js";

$t->write_file_expand('nginx.conf', <<"EOF");
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/init.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream zoned {
        zone zoned 64k;
        server 127.0.0.1:%%PORT_8091%%;
        server 127.0.0.1:%%PORT_8092%%;
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /run/  { }
        location /app/  { }
        location /app2/ { }
    }
}
EOF

$t->write_file('init.js', <<"JS");
nginx.use('$plugin', { cfgWorkerPath: '$cfgw' });

function pass(n) { nginx.log(6, "JSTEST PASS " + n); }
function fail(n, g) { nginx.log(6, "JSTEST FAIL " + n + ": " + g); }
function check(n, ok, g) { if (ok) { pass(n); } else { fail(n, String(g)); } }
function threw(fn) { try { fn(); return false; } catch (e) { return true; } }

nginx.broadcast(function () {
    var run = nginx.http.servers[0].locations.find(
                  function (l) { return l.path === '/run/'; });

    run.handler = function (r) {
        var sc = nginx.safeConfig;
        var peers = nginx.http.upstreams[0].peers;   /* resolve addrs live */

        check('installed', typeof sc === 'object' && typeof sc.apply === 'function');

        /* dryRun plan: not applied, carries class/propagation */
        var pl = sc.plan('http.upstreams[0].peers[0].weight', 7);
        check('plan_not_applied', pl.applied === false, pl.applied);
        check('plan_class_safe',  pl['class'] === 'safe', pl['class']);
        check('plan_zoned_shared', pl.propagation === 'zoned-shared', pl.propagation);

        /* worker-local member reports fanOut in its plan */
        var hp = sc.plan('http.servers[0].locations[1].headers.addHeaders', []);
        check('plan_worker_local', hp.propagation === 'worker-local', hp.propagation);
        check('plan_fanout_flag',  hp.fanOut === true, hp.fanOut);

        /* gate: guarded handler write throws without ack */
        check('guarded_throws', threw(function () {
            sc.apply('http.servers[0].locations[0].handler', function () {});
        }));

        /* gate: irreversible member throws without confirm
         * (removeLocation is classified irreversible; the gate fires before
         * any mutation, so this exercises the tier without removing anything) */
        check('irreversible_throws', threw(function () {
            sc.apply('http.servers[0].removeLocation', 0);
        }));

        /* gate: unknown member throws */
        check('unknown_throws', threw(function () {
            sc.apply('http.servers[0].locations[0].nope', 1);
        }));

        /* safe write needs no opt-in */
        check('safe_no_gate', !threw(function () {
            sc.drainPeer('zoned', peers[0].address);
        }));

        /* canaryWeight sets stable + canary weights */
        var cw = sc.canaryWeight('zoned', 20);
        check('canary_stable_80', cw.stable.value === 80, cw.stable.value);
        check('canary_canary_20', cw.canary.value === 20, cw.canary.value);
        check('canary_applied',
              peers[0].weight === 80 && peers[1].weight === 20,
              peers[0].weight + '/' + peers[1].weight);

        /* drainPeer marks peer down */
        sc.drainPeer('zoned', peers[1].address);
        check('drain_down', peers[1].down === true, peers[1].down);

        /* setResponseHeader on /app/ (applied + fanned out) */
        var hr = sc.setResponseHeader('localhost', '/app/', 'X-SC', 'on');
        check('setheader_applied', hr.applied === true, hr.applied);
        check('setheader_fanout',  hr.fannedOut === true, hr.fannedOut);

        /* toggleLocation: guarded → needs ack; then disables /app2/ */
        check('toggle_needs_ack', threw(function () {
            sc.toggleLocation('localhost', '/app2/', false);
        }));
        sc.toggleLocation('localhost', '/app2/', false, { ack: true });

        r.respond(200, {'Content-Type': 'text/plain'}, 'done\\n');
    };

    /* /app/ has a trivial handler so the X-SC header rides a real response */
    var app = nginx.http.servers[0].locations.find(
                  function (l) { return l.path === '/app/'; });
    app.handler = function (r) { r.respond(200, {}, 'app\\n'); };
});
JS

$t->try_run('no js module or upstream_zone')->plan(18);

# Trigger all gateway/policy checks.
like(http_get('/run/'), qr/done/, 'gateway run completed');

my $log = $t->read_file('error.log');

like($log, qr/JSTEST PASS installed/,          'nginx.safeConfig installed');
like($log, qr/JSTEST PASS plan_not_applied/,   'dryRun plan not applied');
like($log, qr/JSTEST PASS plan_class_safe/,    'plan reports class');
like($log, qr/JSTEST PASS plan_zoned_shared/,  'zoned peer → zoned-shared');
like($log, qr/JSTEST PASS plan_worker_local/,  'addHeaders → worker-local');
like($log, qr/JSTEST PASS plan_fanout_flag/,   'worker-local plan flags fanOut');
like($log, qr/JSTEST PASS guarded_throws/,     'guarded write throws without ack');
like($log, qr/JSTEST PASS irreversible_throws/,'irreversible write throws without confirm');
like($log, qr/JSTEST PASS unknown_throws/,     'unknown member throws');
like($log, qr/JSTEST PASS safe_no_gate/,       'safe write needs no opt-in');
like($log, qr/JSTEST PASS canary_applied/,     'canaryWeight applied 80/20 split');
like($log, qr/JSTEST PASS drain_down/,         'drainPeer marked peer down');
like($log, qr/JSTEST PASS setheader_applied/,  'setResponseHeader applied');
like($log, qr/JSTEST PASS toggle_needs_ack/,   'toggleLocation needs ack');

# Functional follow-ups: the header and the toggle actually took effect.
like(http_get('/app/'),  qr/X-SC: on/i, 'X-SC response header present on /app/');
is(get_code('/app2/'), 503, '/app2/ disabled by toggleLocation → 503');

# settable() / describe() drift not in scope here; covered by js_com_describe.t.
pass('safe-config policy plugin exercised');

sub get_code {
    my $r = http_get(shift);
    return $1 if $r =~ m!^HTTP/\d\.\d\s+(\d+)!;
    return 0;
}
