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
        code: 'sock.mutate',
        why: 'close() / broadcast() on a socket the compartment does not own '
           + '(SR-1 MEDIUM-4): mutating host state, not a scalar read',
        /* ENFORCE, deliberately: in audit the op would be ALLOWED, and the probe
         * would close the listening socket out from under the test. */
        mode: 'enforce',
        probe: "function(){ try { s.close(); return 'closed'; }"
             + " catch (e) { return 'denied'; } }",
        expect: 'denied'
    }
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
        code: 'E_PIN_IDENTITY',
        why: 'the artifact identity pin (R7) did not match the source',
        via: 'include',
        probe: "comcon.include('function(){ return 1; }', "
             + "{imports: [], identity: 'deadbeef'})",
        msg: 'artifact identity mismatch'
    },
    {
        code: 'E_EPOCH_STALE',
        why: 'calling a fragment whose epoch was superseded beyond the '
           + 'rollback window and freed -- an error, never a crash',
        via: 'stale',
        probe: null,          /* built by the test: include, free, then call */
        msg: 'stale epoch'
    }
];

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { GOLDEN: GOLDEN, REFUSALS: REFUSALS };
}
