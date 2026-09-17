// THE VENDOR CODE — three hundred files you may not edit, reduced to one.
//
// A host-side function (real JS) that the auditor can query without running
// it.  It calls `fetch` (a free name) twice, once with a nested argument, and
// it also calls a LOCALLY-BOUND alias `g` -- the call no grant can name,
// which is what source-rewrite hardening exists for.

export default function (x) {
    var a = fetch("https://api.partner.com/v1/a");
    var b = fetch(helper(2));             // a nested argument: still attributed to fetch
    var log = [];
    function real(s) { log.push("RAN:" + s); return s.toUpperCase(); }
    var g = real;                         // a local alias: invisible to grants
    var out = g("a") + g("b");
    return a + b + out + x + log.length;
}
