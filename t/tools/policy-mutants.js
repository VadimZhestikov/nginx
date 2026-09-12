/*
 * V11 — POLICY MUTATION TESTING (VERIFICATION.md: "widen-one-permit mutants
 * must be killed by the deny-suite").
 *
 * This file is the MUTATION GENERATOR. It takes a policy description and emits
 * variants that each grant ONE MORE PERMIT than the base, with a label and the
 * reason that permit matters. It calls no comcon API and makes no assertions:
 * it is data in, data out, so the thing under test is the deny-suite rather
 * than this file.
 *
 * WHAT THE TEST IS ACTUALLY MEASURING. A negative control asks "does this test
 * fail when I break the CODE". Mutation testing asks the harder question: "does
 * the test suite notice when the POLICY gets weaker?" Those differ, and the
 * second is the one that matters for a capability system, where a regression
 * does not look like a crash -- it looks like a permit nobody asked for. Every
 * defect this project found by hand this month had that shape: a typo'd flavor
 * that granted FULL authority, a mode switch that reported success and changed
 * nothing, an admission gate that refused ordinary JS. A surviving mutant is
 * the same finding, arrived at mechanically.
 *
 * EQUIVALENT MUTANTS are declared, not discovered. A mutation that cannot widen
 * anything (adding `eval` to a manifest, when the deny list refuses it whatever
 * the manifest says) MUST survive: it is the control on the classification
 * itself, and a test that killed it would be reporting a permit that does not
 * exist.
 */

function mutants(base) {
    var out = [];

    function mut(label, why, patch, equivalent) {
        var m = { label: label, why: why, equivalent: !!equivalent,
                  policy: JSON.parse(JSON.stringify(base)) };
        patch(m.policy);
        out.push(m);
    }

    /* --- the mediation membrane: one more field through it ---------------- */
    ['address', 'fd', 'listener'].forEach(function (f) {
        mut('mask+' + f,
            'the membrane stops hiding ' + f,
            function (p) { p.allow = p.allow.concat([f]); });
    });

    mut('mask=FULL', 'the membrane hides nothing at all',
        function (p) { p.allow = ['address', 'port', 'fd', 'listener']; });

    mut('unmediated', 'the capability is granted with no membrane',
        function (p) { p.mediated = false; });

    /* --- admission: one more name the fragment may reference -------------- */
    mut('imports+nope', 'an undeclared name becomes declared',
        function (p) { p.imports = p.imports.concat(['nope']); });

    mut('imports+nginx', 'a HOST name becomes admissible',
        function (p) { p.imports = p.imports.concat(['nginx']); });

    mut('intrinsics+JSON', 'the narrowing lets a language value back in',
        function (p) { p.intrinsics = ['JSON']; });

    mut('intrinsics=off', 'the narrowing is dropped entirely',
        function (p) { delete p.intrinsics; });

    mut('checkRequest=off', 'the fragment may read unsealed request fields',
        function (p) { p.checkRequest = false; });

    mut('meter=off', 'the fragment is no longer bounded by the contract meter',
        function (p) { p.meterMs = 0; });

    /* --- declared EQUIVALENT: cannot widen anything ----------------------- */
    mut('imports+eval',
        'the deny list refuses eval whatever a manifest says, so this grants '
        + 'no permit and MUST survive -- it is the control on the '
        + 'classification, not a gap in the suite',
        function (p) { p.imports = p.imports.concat(['eval']); },
        true);

    /* NOT equivalent, and I had it wrong: declaring JSON while the narrowing
     * excludes it DOES widen. `intrinsics` removes the no-declaration free
     * pass; it does not stop a name from being declared, and the admission
     * check falls through to the manifest. So `{intrinsics: [], imports:
     * ['JSON']}` permits JSON -- coherent, but not what "the narrowing excludes
     * JSON" sounds like. The mutation run is what corrected the belief: it
     * killed a mutant I had labelled equivalent. */
    mut('imports+JSON',
        'a name the narrowing excluded is re-admitted by DECLARING it -- the '
        + 'narrowing removes the free pass, not the ability to declare',
        function (p) { p.imports = p.imports.concat(['JSON']); });

    return out;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { mutants: mutants };
}
