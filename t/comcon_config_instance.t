#!/usr/bin/perl

# COMCON M-CFG — one tenant subtree onboarded through admit, end to end.
# The last named deliverable of increment E, and the first thing to compose
# D5b-1's sound rejecter, D3's quotations, M4's typed registry, and std.ops.
#
# THE PROPOSAL NEVER EXECUTES, and that is the design rather than a precaution.
# A COM capability cannot cross into a compartment -- only C-wrapped sockets and
# server facets do -- so a config fragment could not be handed the tree even if
# that were wanted. reviewDeclarative reduces the source to a DESCRIPTOR TABLE
# (inert, diffable) and the OPERATOR applies the table with its own authority.
# "Tenant proposes what it cannot apply; the operator realizes" is then a
# property of the mechanism, not a convention someone has to honour.
#
# REFUSAL IS BY SAFETY CLASS, NOT BY A BLOCKLIST. Every member carries its type
# and class in describe(), so `handler` is refused for what it IS ("rewires
# nginx dispatch", class guarded) and `internal` for being read-only. A COM
# member added next year is classified the day it is added; a blocklist would be
# a list that rots, which is the failure V7 exists to prevent.
#
# The test walks the whole path: propose, review (typed, pinned by hash),
# audit-first diff, refuse what policy forbids, apply what it allows with the
# guarded op named explicitly, verify through HTTP, then roll back exactly.

# NEGATIVE CONTROLS (run 2026-09-12; all six reverted to failure, rebuilt and
# re-passed after each):
#
#   apply() rolls back on a setter refusal   -> the atomicity tests (18-19)
#   the class gate (guarded needs naming)    -> tests 4, 10, 16, 20
#   read-write is required to assign         -> the read-only refusal
#   args are typed against the registry      -> wrongType / functionTyped
#   intermediate segments must be traversable-> notTraversable
#   the subtree root is enforced             -> nearly everything
#
# The first had to be re-aimed: deleting the restore loop left the bootstrap
# syntactically broken, so nginx would not start and prove(1) reported a SKIP --
# a pass that proves nothing. The control is now a valid mutation that disables
# the loop (`for (k = -1; ...)`) instead of removing it.
#
# WHAT THIS FILE FOUND, by composing pieces that each already passed their own
# tests: apply() was NOT atomic. `root` was written, `proxy.pass` was refused by
# the COM setter, and the throw discarded the very snapshot the caller needed to
# undo the first write -- a live subtree left half-configured with no way back.
# The registry cannot prevent it: `proxy.pass` is a well-typed string, and
# whether an upstream EXISTS is not something a type system knows.

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use lib 'lib';
use Test::Nginx;

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy/);

$t->write_file_expand('nginx.conf', <<'EOF');
%%TEST_GLOBALS%%
daemon off;

js_source %%TESTDIR%%/root.js;

events { }

http {
    %%TEST_GLOBALS_HTTP%%

    upstream acme_backend { server 127.0.0.1:8081; }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location /acme  { proxy_pass http://acme_backend; }
        location /ctl   { }
    }
}
EOF

$t->write_file('root.js', <<'JS');
var locs = nginx.http.servers[0].locations;
var acme = locs.find(function (l) { return l.path === "/acme"; });

/* WHAT THE TENANT SENDS: ordinary config-shaped sentences over its own subtree.
 * It is a quotation -- inert, cap-free, and never run. */
var PROPOSAL =
    "acme.root('/srv/acme');"
  + "acme.proxy.connectTimeout(2500);"
  + "acme.proxy.pass('http://acme_backend');";

/* WHAT THE OPERATOR ALLOWS: a subtree, a path allow-list, and the classes that
 * may be applied without being named one by one. */
var POLICY = { type: 'NginxLocation', root: 'acme',
               allow: ['root', 'alias', 'proxy.*'],
               allowClass: ['safe'] };

/* Well-typed by the registry, and still refused by the COM setter: the type
 * system cannot know which upstreams exist. This is what makes apply() need to
 * be all-or-nothing. */
var BAD_PROPOSAL =
    "acme.root('/srv/other');"
  + "acme.proxy.pass('http://no_such_upstream');";

var ctl = locs.find(function (l) { return l.path === "/ctl"; });
var last = null;

ctl.handler = function (req) {
    var op = req.queryParams.op, r = {};
    try {
        if (op === 'review') {
            var plan = comcon.std.config.review(PROPOSAL, POLICY);
            r.ok = plan.ok;
            r.hash = plan.hash;
            r.ops = plan.ops.map(function (o) {
                return o.path + '=' + o.verdict + '/' + o.cls + '/' + o.type; });
            r.refused = plan.refused.map(function (o) {
                return o.path + ': ' + o.why; });
            r.samehash = (comcon.std.config.review(PROPOSAL, POLICY).hash
                          === plan.hash);

        } else if (op === 'diff') {
            /* AUDIT FIRST: what would change, with nothing applied */
            var p2 = comcon.std.config.review(PROPOSAL, POLICY);
            r.diff = comcon.std.config.diff(p2, acme).map(function (d) {
                return d.path + ': ' + JSON.stringify(d.from) + ' -> '
                     + JSON.stringify(d.to) + (d.changes ? ' *' : ''); });
            r.stillRoot = acme.root;

        } else if (op === 'apply-noconfirm') {
            var p3 = comcon.std.config.review(PROPOSAL, POLICY);
            r.out = 'ACCEPTED';
            try { comcon.std.config.apply(p3, acme); }
            catch (e) { r.out = /class guarded/.test(e.message)
                                ? 'needs-confirm' : e.message; }

        } else if (op === 'apply') {
            var p4 = comcon.std.config.review(PROPOSAL, POLICY);
            last = comcon.std.config.apply(p4, acme,
                       { confirm: ['acme.proxy.pass'] });
            r.applied = last.applied;
            r.hash = last.hash;
            r.now = { root: acme.root, httpVersion: acme.proxy.httpVersion,
                      connectTimeout: acme.proxy.connectTimeout,
                      pass: acme.proxy.pass };

        } else if (op === 'rollback') {
            r.restored = comcon.std.config.rollback(last, acme);
            r.now = { root: acme.root, httpVersion: acme.proxy.httpVersion,
                      connectTimeout: acme.proxy.connectTimeout };

        } else if (op === 'atomic') {
            /* the setter refuses mid-way; the subtree must end up untouched */
            var before = acme.root;
            var pb = comcon.std.config.review(BAD_PROPOSAL, POLICY);
            r.reviewOk = pb.ok;
            r.out = 'ACCEPTED';
            try {
                comcon.std.config.apply(pb, acme, { confirm: ['acme.proxy.pass'] });
            } catch (e) { r.out = e.message; }
            r.rootUnchanged = (acme.root === before);
            r.root = acme.root;

        } else if (op === 'refusals') {
            /* Each of these is refused for a DIFFERENT structural reason. */
            var OPEN = { type: 'NginxLocation', root: 'acme',
                         allow: ['*'], allowClass: ['safe'] };
            function why(src, pol) {
                var p = comcon.std.config.review(src, pol || OPEN);
                /* a non-declarative source is rejected WHOLE, with no ops to
                 * point at -- report that rather than calling it accepted */
                if (p.rejected) { return 'non-declarative: ' + p.rejected; }
                if (p.refused.length) { return p.refused[0].why; }
                if (p.ops.length && p.ops[0].verdict === 'confirm') {
                    return 'confirm: ' + p.ops[0].why;
                }
                return 'ACCEPTED';
            }
            /* a function-typed member cannot be EXPRESSED by a declarative
             * sentence at all: its args are literals. Stronger than a class
             * refusal -- there is no way to write the proposal. */
            r.functionTyped = why("acme.handler('x');");
            r.readOnly      = why("acme.proxy('x');");
            r.notTraversable = why("acme.root.deeper('x');");
            r.noSuchMember = why("acme.proxy.nosuch('x');");
            r.wrongType    = why("acme.proxy.connectTimeout('soon');");
            r.outsideTree  = why("other.root('/x');");
            r.notAllowed   = why("acme.alias('/y');",
                                 { type:'NginxLocation', root:'acme',
                                   allow:['proxy.*'], allowClass:['safe'] });
            r.guardedNamed = why("acme.proxy.pass('http://acme_backend');");
            r.notDeclarative = why("acme.root('/x'); for(;;){}");
            /* and NOTHING was applied by any of that */
            r.untouched = acme.root;
        }
    } catch (e) { r.error = e.message; }
    req.respond(200, {'content-type':'application/json'}, JSON.stringify(r));
};
JS

$t->try_run('no js module')->plan(20);

###############################################################################

# --- review: typed, classified, pinned ------------------------------------
my $rv = http_get('/ctl?op=review');
like($rv, qr/"ok":true/, 'the proposal passes review under the policy');
like($rv, qr/"acme.root=ok\/safe\/string"/,
     'a safe string member is typed and admitted from the registry');
like($rv, qr/"acme.proxy.connectTimeout=ok\/safe\/number"/,
     'a nested member resolves THROUGH a read-only handle and is typed as a '
     . 'number -- traversable in the middle is right, assignable only at the end');
like($rv, qr/"acme.proxy.pass=confirm\/guarded\/string"/,
     'a GUARDED member is admitted but demoted to needs-confirmation -- by its '
     . 'class in describe(), not by a blocklist');
like($rv, qr/"samehash":true/,
     'the plan is pinned by hash, and the same source reviews to the same pin');

# --- audit first ----------------------------------------------------------
my $df = http_get('/ctl?op=diff');
# The body is JSON inside JSON, so the inner quotes are backslash-escaped.
like($df, qr/acme\.root: .*-> .*\/srv\/acme.* \*/,
     'diff() shows what WOULD change, read from the live subtree');
like($df, qr/acme\.proxy\.pass: .*acme_backend.* -> .*acme_backend[^*]*"/,
     '...and marks an op that changes NOTHING as changing nothing');
like($df, qr/"acme.proxy.connectTimeout: 60000 -> 2500 \*"/,
     '...with the current value, not an assumed one');
unlike($df, qr/"stillRoot":"\/srv\/acme"/,
     '...and changes nothing: audit-first is the default, not a mode');

# --- the guarded op cannot slip through ----------------------------------
like(http_get('/ctl?op=apply-noconfirm'), qr/"out":"needs-confirm"/,
     'apply() refuses the whole plan until the guarded op is NAMED -- class-3 '
     . 'semantics: snapshot-first, explicit confirmation, or reject');

# --- apply, with the guarded op named ------------------------------------
my $ap = http_get('/ctl?op=apply');
like($ap, qr/"applied":\["acme.root","acme.proxy.connectTimeout","acme.proxy.pass"\]/,
     'the operator applies the table with its OWN authority -- the proposal '
     . 'never ran');
like($ap, qr/"root":"\/srv\/acme"/,             'the subtree took the root');
like($ap, qr/"connectTimeout":2500/,             '...the nested number');
like($ap, qr/"pass":"http:\/\/acme_backend"/,    '...and the guarded upstream');

# --- rollback is exact ----------------------------------------------------
my $rb = http_get('/ctl?op=rollback');
like($rb, qr/"restored":3/, 'rollback restores every applied member');
like($rb, qr/"connectTimeout":60000/,
     '...to what was there BEFORE, not to a default someone guessed');

# --- all-or-nothing: a setter may refuse what the registry accepted -------
my $at = http_get('/ctl?op=atomic');
like($at, qr/"reviewOk":true/,
     'a proposal can be well-typed by the registry and still be wrong: the '
     . 'type system cannot know which upstreams exist');
like($at, qr/"out":"[^"]*acme\.proxy\.pass was refused by the COM setter[^"]*rolled back 1 of 1 write\(s\)[^"]*"/,
     'when the COM setter refuses mid-way, apply() ROLLS BACK what it already '
     . 'wrote and says so -- a live subtree is never left half-configured');
like($at, qr/"rootUnchanged":true/,
     '...so the earlier write is undone, not merely reported');

# --- refusals, each for its own structural reason -------------------------
my $rf = http_get('/ctl?op=refusals');
like($rf,
     qr/"functionTyped":"type: handler is function, got string","readOnly":"not writable: proxy is read-only[^"]*","notTraversable":"not traversable: root is string","noSuchMember":"no such member: proxy.nosuch on NginxProxy","wrongType":"type: proxy.connectTimeout is number, got string","outsideTree":"outside the subtree: expected acme[^"]*","notAllowed":"not in the policy allow-list: alias","guardedNamed":"confirm: class guarded[^"]*","notDeclarative":"non-declarative: [^"]*"/,
     'with a permissive allow-list each source is refused for its OWN reason: '
     . 'function-typed (unexpressible in a declarative sentence at all), '
     . 'read-only, non-traversable, unknown member, wrong type, wrong subtree, '
     . 'allow-list, and non-declarative');
