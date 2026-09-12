/*
 * V3 — an EXECUTABLE REFERENCE SEMANTICS for the COMCON kernel rules
 * (VERIFICATION.md: "the highest-value first step is not Coq: it is an
 * executable reference implementation of the kernel rules... used as an oracle,
 * differentially tested against the real engine on every admission-relevant
 * operation -- catching IMPLEMENTATION DRIFT FROM THE MODEL, which proofs of the
 * model alone never see").
 *
 * This file is the MODEL. It is deliberately written from the RULES as stated in
 * SEMANTICS/FOUNDATION, not from the implementation -- an oracle derived from the
 * code under test agrees with it by construction and detects nothing. It shares
 * no code with src/js: it is plain data manipulation over a description of a
 * case, and it never calls comcon.
 *
 * It predicts, for a case {grants, mediations, source}:
 *   admitted   - does admission accept the fragment
 *   bound      - which names the fragment actually sees
 *   visible    - per granted socket name, which fields are readable
 * and t/comcon_v3_oracle.t runs the same cases through the real engine and
 * compares field by field.
 *
 * THE RULES MODELLED (each is a claim the implementation must honour):
 *   R-ENV     a fragment sees exactly the names granted to it; every other free
 *             name is unbound.
 *   R-ADMIT   admission is ON iff the contract declares imports (presence, not
 *             truthiness); with it on, EVERY free global outside the manifest
 *             refuses the fragment -- including language intrinsics. There is a
 *             deny list (eval, Function, globalThis, global, self) that no
 *             manifest can re-admit, and NO intrinsics allowance: `undefined`,
 *             `JSON`, `Object` and friends are free globals like any other and
 *             must be declared. That surprised this oracle into existence --
 *             see t/comcon_v3_oracle.t and VERIFICATION.md V3.
 *   R-MEDIATE the mediation vocabulary is CLOSED. revoke withholds the name
 *             entirely; allow/redact are field masks; an unknown flavor is
 *             refused (never "full authority").
 *   R-MEET    re-mediation is the MEET: A(cap'') = A(cap') AND A(cap). An outer
 *             membrane can only narrow.
 *   R-ZERO    revoke is the zero: it absorbs anything met with it.
 */

var FIELDS = { address: 1, port: 2, fd: 4, listener: 8 };
var FULL = 15;
var KNOWN = { revoke: 1, redact: 1, allow: 1, routes: 1 };

function maskOf(m) {                       /* R-MEDIATE: flavor -> field mask */
    if (!m) { return FULL; }
    if (!KNOWN[m.flavor]) { return null; }              /* refused, not FULL */
    if (m.flavor === 'revoke') { return 0; }
    if (m.flavor === 'routes') { return FULL; }
    var bits = 0, i;
    var fs = m.fields || [];
    if (m.flavor === 'allow') {
        for (i = 0; i < fs.length; i++) { bits |= (FIELDS[fs[i]] || 0); }
        return bits;
    }
    bits = FULL;
    for (i = 0; i < fs.length; i++) { bits &= ~(FIELDS[fs[i]] || 0); }
    return bits;
}

/* A grant is {name, mediations: [inner, ..., outer]} -- the list is applied in
 * order, each one attenuating the one before (R-MEET). */
function authority(grant) {
    var mask = FULL, i, m, one;
    var meds = grant.mediations || [];
    for (i = 0; i < meds.length; i++) {
        m = meds[i];
        if (m && !KNOWN[m.flavor]) { return { refused: true }; }
        one = maskOf(m);
        if (one === null) { return { refused: true }; }
        mask &= one;                                   /* R-MEET: meet is AND */
        if (m && m.flavor === 'revoke') { mask = 0; }  /* R-ZERO: absorbing   */
    }
    var withheld = false;
    for (i = 0; i < meds.length; i++) {
        if (meds[i] && meds[i].flavor === 'revoke') { withheld = true; }
    }
    return { mask: mask >>> 0, withheld: withheld };
}

/* Free names a source uses, by the same shape the corpus generates: `NAME.field`
 * and bare `NAME`. The model does not parse JS -- the corpus states which names
 * a case reads, and this only checks them against the environment (R-ENV). */
function predict(kase) {
    var out = { admitted: true, refusedBy: null, bound: [], visible: {} };
    var i, g, a;

    for (i = 0; i < kase.grants.length; i++) {
        g = kase.grants[i];
        a = authority(g);
        if (a.refused) {
            out.admitted = false;
            out.refusedBy = 'R-MEDIATE: unknown flavor in grant ' + g.name;
            return out;
        }
        if (a.withheld) { continue; }        /* R-MEDIATE: revoke withholds */
        out.bound.push(g.name);
        out.visible[g.name] = {};
        for (var f in FIELDS) {
            if (Object.prototype.hasOwnProperty.call(FIELDS, f)) {
                out.visible[g.name][f] = !!(a.mask & FIELDS[f]);
            }
        }
    }

    /* R-ADMIT: admission is on iff imports is PRESENT. */
    var DENIED = { eval: 1, Function: 1, globalThis: 1, global: 1, self: 1 };
    if (kase.imports !== undefined) {
        for (i = 0; i < kase.reads.length; i++) {
            if (DENIED[kase.reads[i]]) {
                out.admitted = false;
                out.refusedBy = 'R-ADMIT: denied name (no manifest re-admits '
                                + 'it): ' + kase.reads[i];
                return out;
            }
            if (kase.imports.indexOf(kase.reads[i]) < 0) {
                out.admitted = false;
                out.refusedBy = 'R-ADMIT: free name outside the manifest: '
                                + kase.reads[i];
                return out;
            }
        }
    }
    return out;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { predict: predict, authority: authority, maskOf: maskOf };
}
