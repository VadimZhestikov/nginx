/*
 * V13 — the ERASURE corpus: one source, two engines.
 *
 * Principle 11 says COMCON extends by GRANTING, never by changing the language:
 * admission annotations (the free-name manifest, the intrinsics narrowing, the
 * sealed-request check, the typed profile) are meant to decide WHETHER code runs,
 * never WHAT it computes. That is the base case the whole compiled tier rests on
 * -- if annotations could change semantics, T1 ≡ T2 would be comparing two
 * different programs.
 *
 * So each row below is run twice: admitted and confined inside COMCON, and in
 * plain NODE with no annotations and no confinement at all. The outputs must be
 * identical. A different ENGINE is the point -- an in-process comparison shares
 * the very runtime whose behaviour is in question.
 *
 * The rows are chosen where erasure could plausibly break rather than where it
 * obviously holds: the numeric model at its boundaries (V1: JS doubles are
 * normative on both tiers, so 2^53, -0 and NaN must behave identically), string
 * and JSON round-trips, RegExp, sort stability, and property enumeration order.
 * `arg` crosses the COMCON boundary as JSON, so it is JSON-shaped on purpose.
 */

var ERASURE = [
    { name: 'numeric-safe-integer-boundary',
      why: 'V1 makes JS double semantics normative on both tiers; 2^53 is where '
         + 'a naive int64 would diverge',
      src: "function(a){ var n = 9007199254740992;"
         + " return [n, n + 1, n === n + 1, (n + 2) - n, a.k + 1]; }",
      arg: { k: 9007199254740991 } },

    { name: 'negative-zero-and-nan',
      why: '-0 and NaN are where a compiler is most tempted to normalize',
      /* String(), not the raw value: JSON.stringify turns -Infinity and NaN
         into null on BOTH sides, so comparing them as numbers would agree no
         matter what the engines did -- a vacuous row dressed as a strict one. */
      src: "function(a){ var z = -0, n = 0/0;"
         + " return [String(1/z), String(n), Object.is(z, -0), n === n,"
         + "         [n].indexOf(n), [z].indexOf(0), Math.min(0, -0) === 0,"
         + "         String(Object.is(0, -0))]; }",
      arg: {} },

    { name: 'string-and-json-roundtrip',
      why: 'escaping is where two engines most often differ in practice',
      src: "function(a){ var s = a.s;"
         + " return [s.length, JSON.stringify(s), JSON.parse(JSON.stringify(a)).s === s,"
         + "         s.toUpperCase(), encodeURIComponent(s)]; }",
      arg: { s: "a\"b\\c\nd\teéf/g" } },

    { name: 'regexp-capture-and-lastindex',
      why: 'a stateful builtin: the same source must leave the same state',
      src: "function(a){ var re = /(\\d+)-(\\w+)/g, out = [], m;"
         + " while ((m = re.exec(a.s)) !== null) { out.push([m[1], m[2], m.index, re.lastIndex]); }"
         + " return out; }",
      arg: { s: "12-ab 345-cde 6-f" } },

    { name: 'sort-and-enumeration-order',
      why: 'array sort comparator semantics and integer-key ordering',
      src: "function(a){ var xs = a.xs.slice().sort(function(p,q){ return p - q; });"
         + " var o = {}, i; for (i = 0; i < a.xs.length; i++) { o[a.xs[i]] = i; }"
         + " return [xs, Object.keys(o), JSON.stringify(o)]; }",
      arg: { xs: [10, 2, 33, 4, 1, 20] } },

    { name: 'closure-and-exception-control-flow',
      why: 'try/catch/finally ordering and closure capture in a loop',
      src: "function(a){ var log = [], fns = [], i;"
         + " for (i = 0; i < 3; i++) { (function(j){ fns.push(function(){ return j; }); })(i); }"
         + " try { log.push('t'); throw new Error('x'); }"
         + " catch (e) { log.push('c:' + e.message); }"
         + " finally { log.push('f'); }"
         + " return [log, fns.map(function(f){ return f(); }), a.n | 0, ~~a.n]; }",
      arg: { n: 3.9 } },

    { name: 'date-free-arithmetic',
      why: 'no clock is granted to a fragment (Date is deliberately not an '
         + 'intrinsic), so the corpus must compute without one -- this row '
         + 'exists to prove the corpus itself is deterministic',
      src: "function(a){ var t = 0, i; for (i = 1; i <= 100; i++) { t += i * i; }"
         + " return [t, t % 7, (t / 3).toFixed(6), String(t)]; }",
      arg: {} }
];

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ERASURE: ERASURE };
}
