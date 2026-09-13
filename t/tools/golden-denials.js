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
 * WHAT IS FROZEN HERE IS WHAT EXISTS. The compartment denial codes are real,
 * closed, and machine-parsable (`nginx.tenantDenials().byOp`). The ADMISSION
 * refusals are not codes at all -- they are message text -- so a tenant cannot
 * pin CI to them today, which is precisely what the manual tells them not to do.
 * MANUAL §3.2 marks the code taxonomy **[TBD-2]** ("E_CAP_*, E_ADMIT_*,
 * E_BUDGET_*, E_PIN_*, E_EPOCH_*" are candidate families, undesigned). The
 * `provisional` rows below record today's message prefixes so the gap is
 * visible and dated -- they are NOT a contract, and the test says so.
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

/* NOT codes. Today's admission refusals are message text; recorded so the
 * absence of a code taxonomy is visible and dated, not so anyone pins to them. */
var PROVISIONAL = [
    { prefix: 'free name not declared in imports',
      probe: "function(){ return typeof nosuchhostname; }", imports: [] },
    /* A direct eval CALL, not a reference to `eval`: a reference is caught by
     * the deny list as a free name and never reaches the dynamic-code check, so
     * the first version of this row froze the wrong refusal. (`with` is the
     * other trigger and cannot be probed: include()'s wrapper is "use strict",
     * where `with` is a syntax error.) */
    { prefix: 'dynamic-code: eval or with',
      probe: "function(){ return eval('1+1'); }", imports: [] },
    { prefix: 'request field not in sealed schema',
      probe: "function(req){ return req.bogusField; }", imports: [],
      checkRequest: true }
];

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { GOLDEN: GOLDEN, PROVISIONAL: PROVISIONAL };
}
