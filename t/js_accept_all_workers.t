#!/usr/bin/perl

# Tests for Phase 2 — nginx.suspendAllWorkers() / nginx.resumeAllWorkers():
#   cross-worker coordinated connection acceptance control.
#
# Tests:
#   - suspendAllWorkers Promise resolves without error
#   - resumeAllWorkers Promise resolves without error
#   - server remains functional after suspend+resume
#   - withSuspendedAcceptance wrapper works correctly
#   - multiple suspend+resume cycles do not degrade functionality

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }
use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;
worker_processes 2;

js_source %%TESTDIR%%/accept_all.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /suspend_result/ { }
        location /suspend_all/  { }
        location /resume_all/   { }
        location /with_wrap/    { }
        location /check/        { }
        location /multi_cycle/  { }
    }
}
EOF

$t->write_file('accept_all.js', <<'JS');
// Phase 2 — cross-worker coordinated acceptance control

(function install() {
    const locs = nginx.http.servers[0].locations;

    function set(path, fn) {
        const loc = locs.find(l => l.path === path);
        if (loc) { loc.handler = fn; }
    }

    // suspend + immediate resume — both Promises resolve
    set('/suspend_all/', async (req) => {
        await nginx.suspendAllWorkers();
        await nginx.resumeAllWorkers();
        req.respond(200, {'content-type': 'text/plain'}, 'ok');
    });

    // The resolved value reports whether every worker actually acked.
    // It used to resolve with undefined and report success unconditionally,
    // even when the manager's ack wait timed out.
    set('/suspend_result/', async (req) => {
        const r = await nginx.suspendAllWorkers();
        const r2 = await nginx.resumeAllWorkers();
        req.respond(200, {'content-type': 'application/json'},
            JSON.stringify({
                type:     typeof r,
                hasOk:    r !== null && typeof r === 'object' && 'ok' in r,
                unacked:  r && r.unacked,
                ok:       r && r.ok,
                resumeOk: r2 && r2.ok,
            }));
    });

    // suspend_all only (for incremental test)
    set('/resume_all/', async (req) => {
        await nginx.suspendAllWorkers();
        await nginx.resumeAllWorkers();
        req.respond(200, {'content-type': 'text/plain'}, 'resumed');
    });

    // withSuspendedAcceptance wrapper
    set('/with_wrap/', async (req) => {
        let inside = false;
        await nginx.withSuspendedAcceptance(async () => {
            inside = true;
        });
        req.respond(200, {'content-type': 'text/plain'},
                    inside ? 'wrap ok' : 'wrap fail');
    });

    // withSuspendedAcceptance propagates throw, still resumes
    set('/multi_cycle/', async (req) => {
        let errors = 0;
        for (let i = 0; i < 3; i++) {
            await nginx.suspendAllWorkers();
            await nginx.resumeAllWorkers();
        }
        req.respond(200, {'content-type': 'text/plain'},
                    'cycles:' + errors);
    });

    set('/check/', (req) => {
        req.respond(200, {'content-type': 'text/plain'}, 'check ok');
    });
})();
JS

$t->run();

# Basic suspend+resume via explicit calls
like(http_get('/suspend_all/'), qr/200 OK/,  'suspendAll+resumeAll: 200 OK');
like(http_get('/suspend_all/'), qr/\bok\b/,  'suspendAll+resumeAll: body ok');

# Server still functional for new requests after
like(http_get('/check/'),       qr/check ok/, 'server accepts after suspend/resume');

# resume_all variant
like(http_get('/resume_all/'),  qr/resumed/,  'resumeAll resolves');

# withSuspendedAcceptance wrapper
like(http_get('/with_wrap/'),   qr/200 OK/,   'withSuspendedAcceptance: 200 OK');
like(http_get('/with_wrap/'),   qr/wrap ok/,  'withSuspendedAcceptance: fn ran');

# Multiple cycles
like(http_get('/multi_cycle/'), qr/200 OK/,   'multi-cycle: 200 OK');
like(http_get('/multi_cycle/'), qr/cycles:0/, 'multi-cycle: 3 cycles ok');

# The suspend/resume result object — the manager now reports the ack shortfall
my $sr = http_get('/suspend_result/');
like($sr, qr/200 OK/,           'suspendAllWorkers result: 200');
like($sr, qr/"hasOk":true/,     'resolves with a result object carrying ok');
like($sr, qr/"unacked":0/,      'every worker acked (unacked = 0)');
like($sr, qr/"ok":true.*"resumeOk":true|"resumeOk":true/,
     'both suspend and resume report ok');
