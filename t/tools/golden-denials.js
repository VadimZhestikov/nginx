/*
 * V12 — the GOLDEN DENIAL-CODE CORPUS (VERIFICATION.md: "denial codes are the
 * tenants' CI contract (MANUAL §3.2); a frozen (probe → expected code) corpus
 * verifies code stability across releases").
 *
 * MANUAL §3.2 tells tenants: "Codes are stable across releases -- pin your CI to
 * codes, not to message text." This file is what makes that sentence checkable.
 * Each row is a code, a probe that provokes it, and the mode the probe needs;
 * the test runs them and compares the per-code counters before and after.
 *
 * TWO AXES, NOT ONE. A DENIAL code (`GOLDEN`) names the gate that fired while a
 * fragment was RUNNING -- read from `nginx.tenantDenials().byOp`. A REFUSAL code
 * (`REFUSALS`) names why a fragment was never admitted at all -- read from
 * `e.code` on the thrown error, or `code` on an `admit()` verdict, and
 * enumerated by `comcon.refusalCodes()`. A tenant's CI needs both and needs them
 * kept apart: "my policy tripped sock.listener on request 41" and "my policy
 * will not load" are different failures with different fixes.
 *
 * The refusal half was MESSAGE TEXT until [TBD-2] shipped (FOUNDATION v5.62).
 * V12 dated that gap; the rows below are what closed it, and they are frozen on
 * the same terms as the denial codes.
 *
 * A row may be `unreachable` instead of carrying a probe. That is not a gap in
 * the corpus: a gate can be defence-in-depth for a path that does not exist yet,
 * and the honest record is the reason, not a missing row. The enumeration check
 * in t/tools/check-enumerations.py requires every code in the C enum to appear
 * here in one form or the other, so a new code cannot be added without someone
 * either probing it or saying why they cannot.
 */

var GOLDEN = [
    {
        code: 'sock.listener',
        why: 'the sock->listener reach edge, the entry to the '
           + 'sock->listener->serverByName->server->addLocation cycle',
        mode: 'enforce',
        probe: "function(){ return (s.listener === null) ? 'denied' : 'allowed'; }",
        expect: 'denied'
    },
    {
        code: 'listener.read',
        why: 'the listener getters (socket / serverNames), gated on the socket owner',
        /* AUDIT, because the fragment must first HOLD a listener, and in enforce
         * mode sock.listener hands back null. So this probe necessarily trips
         * sock.listener on the way -- declared in `also` rather than hidden. */
        mode: 'audit',
        also: ['sock.listener'],
        probe: "function(){ var l = s.listener;"
             + " if (!l) return 'no-listener';"
             + " return typeof l.serverNames; }",
        expect: null           /* whatever it returns; the counter is the claim */
    },
    {
        code: 'listener.serverByName',
        why: 'the listener->server escalation',
        mode: 'audit',
        also: ['sock.listener', 'listener.read'],
        probe: "function(){ var l = s.listener;"
             + " if (!l) return 'no-listener';"
             + " try { return typeof l.serverByName('localhost'); }"
             + " catch (e) { return 'threw'; } }",
        expect: null
    },
    {
        code: 'enum.sockets',
        why: 'host introspection: cycle.sockets / http.sockets enumeration',
        unreachable:
            'A confined fragment cannot obtain `nginx.http` or `nginx.cycle`: '
          + 'only C-wrapped sockets and server facets cross into a compartment, '
          + 'and neither exposes the socket table. The gate is defence-in-depth '
          + 'for a path that does not exist today -- it fires for any '
          + 'compartment that is not HOST_ROOT, so it would begin working the '
          + 'day such a path is added, which is the point of leaving it in.'
    },
    {
        code: 'budget.uses',
        why: 'a `uses` budget is exhausted (M-LIB): the capability was real and '
           + 'the caller was entitled to it, but not this many times. The only '
           + 'denial here that is about RATE rather than REACH',
        mode: 'enforce',
        /* limit 2 over a 60s window, spent by three reads of a budgeted field;
           the third is the one that must be denied. */
        probe: "function(){ var seen = [];"
             + " seen.push(typeof s.address); seen.push(typeof s.address);"
             + " seen.push(typeof s.address); return seen.join(','); }",
        expect: 'string,string,undefined',
        budget: { key: 'v12-probe', limit: 2, window: 60 }
    },
    {
        code: 'cap.expired',
        why: 'a `ttl` capability lifetime has passed. This is what makes a '
           + 'SESSION LEASE bite on authority already handed out: the TM-2 '
           + 'mapping expires by itself, but include() binds grants at '
           + 'admission, so without this a fragment holds its capabilities for '
           + 'as long as it lives',
        mode: 'enforce',
        /* granted with a 1-second lifetime and probed after it: the fixture
           sleeps between requests, because ngx_time() is nginx's CACHED clock
           and nothing expires inside a single handler. */
        probe: "function(){ return (typeof s.address === 'string')"
             + " ? 'alive' : 'expired'; }",
        expect: 'expired',
        ttl: 1,
        sleepBefore: 1.4
    },
    {
        code: 'sock.mutate',
        why: 'close() / broadcast() on a socket the compartment does not own '
           + '(SR-1 MEDIUM-4): mutating host state, not a scalar read',
        /* ENFORCE, deliberately: in audit the op would be ALLOWED, and the probe
         * would close the listening socket out from under the test. */
        mode: 'enforce',
        probe: "function(){ try { s.close(); return 'closed'; }"
             + " catch (e) { return 'denied'; } }",
        expect: 'denied'
    },
  /* M-LIB `allowHosts` — the outbound capability's two gates.
   *
   * `out.host` is the mediation biting: a destination outside the glob.
   * `out.drain` is the A1 reach gate on the HOST's half of the capability --
   * pending()/clear() read the recorded queue, and a fragment that could drain
   * it would read what a sibling fragment sharing the cap had recorded.  Both
   * rows carry their own probe rather than a written reason, because both are
   * reachable from a fragment in one request. */
  /* `cap: 'outbound'` selects which capability the harness grants, and `grant`
   * names it.  Every earlier row is a socket granted as `s`; these are the first
   * rows of a second kind, so the field had to exist rather than the probe text
   * pretending a socket is an outbound cap. */
  { code: 'out.host', cap: 'outbound', grant: 'out', mode: 'enforce',
    probe: "function(a){ var r = out.request('https://evil.net/x');"
         + " return (r === undefined) ? 'denied' : 'allowed'; }",
    expect: 'denied' },
  /* M-LIB `window` — the recurring sibling of cap.expired.  Its capability is
   * COMPUTED by the harness (`windowClosed`): a window that is reliably closed
   * cannot be written as a constant, since an empty day mask is refused and a
   * one-minute slot would be a flake. */
  { code: 'cap.window', cap: 'windowClosed', grant: 's', mode: 'enforce',
    probe: "function(a){ var v = s.address;"
         + " return (v === undefined) ? 'denied' : 'allowed'; }",
    expect: 'denied' },
  /* M-LIB `cosign` — the two-person rule.  Its capability is also COMPUTED
   * (`cosignSolo`), for a reason unlike the window's: a cosigned capability with
   * ONE acting principal is denied no matter how many times this row runs, since
   * the record is the SET of consenting principals and a principal joins it once.
   * So the row is reliably denied without needing a fresh key each time -- the
   * distinctness rule doing double duty as the test's determinism. */
  { code: 'cap.cosign', cap: 'cosignSolo', grant: 's', mode: 'enforce',
    probe: "function(a){ var v = s.address;"
         + " return (v === undefined) ? 'denied' : 'allowed'; }",
    expect: 'denied' },
  /* M-LIB `protocol` — enforced operation ORDER.  Its capability is COMPUTED
   * (`protoWrong`) and reliably denied for the simplest possible reason: the
   * protocol's only step is `port`, and the probe reads `address`.  No clock, no
   * quorum, no budget -- the order alone. */
  { code: 'cap.protocol', cap: 'protoWrong', grant: 's', mode: 'enforce',
    probe: "function(a){ var v = s.address;"
         + " return (v === undefined) ? 'denied' : 'allowed'; }",
    expect: 'denied' },
  /* A capability used by a fragment it was not granted to.
   *
   * The only reachable way to hold one is a LEFTOVER CONTINUATION: a job queued
   * by fragment A and run inside B's invocation holds A's wrappers.  So this row
   * has a shape of its own (`leftover`), like the `ttl` row's two-phase
   * `sleepBefore`: the probe deliberately queues MORE jobs than the drain's
   * budget, and what fires is measured during the NEXT fragment's invocation.
   *
   * It is the first code in this corpus that names a STRUCTURAL invariant rather
   * than a policy the operator wrote -- nobody configures it, and nothing
   * legitimate trips it. */
  { code: 'cap.owner', cap: 'outbound', grant: 'out', mode: 'enforce',
    leftover: true,
    probe: "function(a){ var i; for (i = 0; i < 10100; i++) {"
         + " Promise.resolve().then(function(){"
         + "   out.request('https://a.example.com/x'); }); }"
         + " return 'queued'; }",
    expect: 'queued' },
  { code: 'out.drain', cap: 'outbound', grant: 'out', mode: 'enforce',
    probe: "function(a){ var r = out.pending();"
         + " return (r === undefined) ? 'denied' : 'drained'; }",
    expect: 'denied' },
];

/*
 * The REFUSAL codes ([TBD-2]). A fragment is refused at ADMISSION -- before it
 * ever runs -- and every such refusal now carries a code in three places: as
 * `.code` on the thrown Error, bracketed at the end of the message (so the
 * error log is greppable), and, for `admit()`, as `code` on the verdict object.
 *
 * `via` says which operator provokes it, because the two report differently by
 * design: `admit()` RETURNS a verdict ({certified, code, reject}) -- it is the
 * question "would this be admitted?" -- while `include()` THROWS, because it
 * was asked to install something and could not.
 *
 * WHERE THE LINE IS DRAWN. A code is warranted where the refusal is a POLICY
 * OUTCOME about a fragment -- something a deploy pipeline should assert on. It
 * is NOT warranted for a malformed call into a library function (`query: empty
 * selector`, `std.config.apply: arg0 must be a plan`): the fix there is to fix
 * the call, and coding it would invite CI to pin to our argument checks. Naming
 * everything would make the taxonomy mean nothing.
 *
 * STILL UNCODED, recorded rather than invented (the std.ops `host:null`
 * discipline -- a gap you can see is a gap someone can close):
 *   - E_BUDGET_* : the deadline abort is the ENGINE's interrupt. There is no
 *     refusal of ours at that point to label, so no code is claimed for it.
 *   - E_CAP_FLAVOR / E_CAP_ESCALATE : the capability layer's own refusals
 *     (mediate()'s closed vocabulary; realize()'s least-authority sub-map
 *     assertion) are thrown in the JS bootstrap, not the host. They are policy
 *     outcomes and DO deserve codes -- the next tranche, not this one.
 */
var REFUSALS = [
    {
        code: 'E_ADMIT_ARG',
        why: 'admit() was handed something that is not a function at all',
        via: 'admit',
        probe: "comcon.admit(42, {imports: []})",
        msg: 'arg0 must be a function'
    },
    {
        code: 'E_ADMIT_NOTBYTECODE',
        why: 'a function, but not one with bytecode to scan -- a host C '
           + 'function has no free-name manifest to check, so it cannot be '
           + 'certified either way; refusing beats guessing',
        via: 'admit',
        probe: "comcon.admit(Math.max, {imports: []})",
        msg: 'not a bytecode function'
    },
    {
        code: 'E_ADMIT_SOURCE',
        why: 'include() source that does not compile to a function expression',
        via: 'include',
        probe: "comcon.include('42', {imports: []})",
        msg: 'source must be a function expression'
    },
    {
        code: 'E_ADMIT_DYNCODE',
        why: 'C3: direct eval (or `with`) inside the fragment',
        via: 'include',
        probe: "comcon.include(\"function(){ return eval('1+1'); }\", "
             + "{imports: []})",
        msg: 'dynamic-code'
    },
    {
        code: 'E_ADMIT_FREENAME',
        why: 'C3 deny-by-default: a free global not declared in `imports`',
        via: 'include',
        probe: "comcon.include('function(){ return nosuchhostname; }', "
             + "{imports: []})",
        msg: 'free name not declared in imports'
    },
    {
        code: 'E_ADMIT_INTRINSIC',
        why: '`intrinsics` names something outside the allowance; it only '
           + 'NARROWS, so accepting the name would leave a contract word that '
           + 'reads like policy and does nothing',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', "
             + "{imports: [], intrinsics: ['nosuchintrinsic']})",
        msg: 'is not in the intrinsics allowance'
    },
    {
        code: 'E_ADMIT_SCHEMA',
        why: 'checkRequest: a request field outside the sealed schema',
        via: 'include',
        probe: "comcon.include('function(req){ return req.bogusField; }', "
             + "{imports: [], checkRequest: true})",
        msg: 'request field not in sealed schema'
    },
    {
        code: 'E_ADMIT_TEST',
        why: 'a contract test threw -- BEHAVIOURAL admission, run against the '
           + 'compiled fragment inside the compartment',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', {imports: [], "
             + "tests: 'function(f){ throw new Error(\"nope\"); }'})",
        msg: 'test failed'
    },
    {
        code: 'E_ADMIT_CONTRACT',
        why: 'a contract field is PRESENT BUT UNUSABLE. This row exists '
           + 'because writing the E_ADMIT_TEST probe above found the defect: '
           + '`tests` was read with a bare string test and silently ignored '
           + 'otherwise, so `tests: [fn]` -- the spelling the plural key '
           + 'invites -- was ADMITTED with the behavioural gate never run. A '
           + 'contract asking to be checked must not be admitted unchecked',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', {imports: [], "
             + "tests: ['function(f){ throw new Error(\"nope\"); }']})",
        msg: 'must be a function(fragment)'
    },
    {
        code: 'E_ADMIT_DEP',
        why: 'a pinned pure-library dependency failed to load or failed its '
           + 'hash pin -- the supply-chain gate',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', {imports: [], "
             + "deps: [{name: 'lib', path: '/nonexistent/comcon-v12.js', "
             + "sha256: '00'}]})",
        msg: 'comcon.include'
    },
    {
        code: 'E_CAP_GRANT',
        why: 'a grant that is not a mediatable capability. The membrane\'s '
           + 'TYPE rule is policy, not an argument check: what may cross into '
           + 'a compartment is exactly what the host can wrap',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', "
             + "{imports: [], grants: {x: {not: 'a cap'}}})",
        msg: 'grant is not a NginxSocket or NginxServer'
    },
    {
        code: 'E_CAP_FLAVOR',
        why: 'a mediation flavor outside the closed vocabulary. This is the '
           + 'refusal that closed a FAIL-OPEN: an unimplemented word '
           + '(`allowHosts`) or a one-letter typo (`redcat` for `redact`) used '
           + 'to fall through the translation and grant the capability IN FULL',
        via: 'call',
        probe: "comcon.mediate(nginx.http.servers[0], {flavor: 'redcat'})",
        msg: 'unknown interceptor flavor'
    },
    {
        code: 'E_CAP_ESCALATE',
        why: 'a composition that cannot be SHOWN to narrow -- one code, one '
           + 'rule, four raising sites. TWO ARE REACHABLE and pinned: a routes '
           + 'glob with no computable meet (probed here, and in '
           + 'comcon_v4_monotonicity.t) and a budget with no computable meet '
           + '(comcon_budget_uses.t). TWO ARE DEFENCE IN DEPTH and cannot be '
           + 'provoked through the public API: realize() builds the restricted '
           + 'map from the realizer\'s own env so it cannot disagree with it, '
           + 'and the mask meet is an AND of two masks. A probe for those would '
           + 'be dead code pretending to be a test',
        via: 'call',
        probe: "comcon.mediate(comcon.mediate(nginx.http.servers[0], "
             + "comcon.routes('/a/*')), comcon.routes('/b/*'))",
        msg: 'a glob meet is not computable'
    },
    {
        code: 'E_CAP_PRINCIPAL',
        why: 'a cosign() with no acting principal. Deliberately NOT folded '
           + 'into E_CAP_FLAVOR: the flavour is spelled correctly and nothing '
           + 'composed, so neither of the capability layer\'s other two codes '
           + 'fits -- what happened is that the policy is incoherent on its own '
           + 'terms. A two-person rule with nobody identified is not a weak '
           + 'two-person rule, it is no rule, and the one direction it must '
           + 'never take is degrading to single-signed',
        via: 'call',
        probe: "comcon.cosign({key: 'k', quorum: 2, within: 60})",
        msg: 'needs `as`'
    },
    {
        code: 'E_PIN_IDENTITY',
        why: 'the artifact identity pin (R7) did not match the source',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', "
             + "{imports: [], identity: 'deadbeef'})",
        msg: 'artifact identity mismatch'
    },
    {
        code: 'E_INVOKE_PENDING',
        why: 'an async fragment whose promise is still PENDING after the '
           + 'compartment\'s own jobs have run. THE ONE CODE ON THIS AXIS '
           + 'RAISED AFTER THE FRAGMENT RAN, and deliberately so: a DENIAL '
           + 'names a gate that refused authority the fragment reached for, '
           + 'and nothing was refused here. The fragment awaited something no '
           + 'amount of running it can settle, because a compartment reaches '
           + 'no timer and no socket. What the tenant must change is in their '
           + 'fragment, which is what this axis names. The alternative was '
           + 'JSON.stringify on a pending promise -- "{}", a '
           + 'plausible-looking empty object',
        via: 'call',
        probe: "comcon.include('function(a){ return new Promise("
             + "function(){}); }', {imports: ['Promise']})({})",
        msg: 'still pending'
    },
    {
        code: 'E_EPOCH_STALE',
        why: 'calling a fragment whose epoch was superseded beyond the '
           + 'rollback window and freed -- an error, never a crash',
        via: 'stale',
        probe: null,          /* built by the test: include, free, then call */
        msg: 'stale epoch'
    },
    {
        code: 'E_AUTHOR_LIMIT',
        why: 'the authoring tier: a fragment holding an `author` capability '
           + 'asked it for more than it carries -- a sub-fragment beyond its '
           + 'subFragments budget, or one authored from inside a '
           + 'sub-fragment. Raised INSIDE the parent fragment, at its own '
           + 'author.include() call; a parent that does not catch it hands '
           + 'it to the host as its failure, and the code survives that '
           + 'boundary as `e.code` because the host copies a string code off '
           + 'the compartment exception. The probe grants a budget of ONE, holds '
           + 'one callable, and asks for a second: subFragments is a LIVE count, '
           + 'so the first must be HELD for the second to refuse',
        via: 'call',
        /* the grant is `author` and the fragment takes no parameter: a grant
           and the invocation argument sharing one name is the argument
           shadowing the grant, which the first version of this probe did */
        probe: "comcon.include("
             + "'function(){ var held = author.include(\"function(){ return 1; }\", "
             + "{imports: []}); "
             + "author.include(\"function(){ return 2; }\", {imports: []}); "
             + "return typeof held; }', "
             + "{imports: [], grants: {author: comcon.author({subFragments: 1})}})"
             + "({})",
        msg: 'sub-fragment budget'
    }
];

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { GOLDEN: GOLDEN, REFUSALS: REFUSALS };
}
