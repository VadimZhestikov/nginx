// A pure library, as a vendor ships it: a bare script whose completion value is
// the object the fragment receives as its named dependency.  No I/O, no
// globals, no eval -- and pinned by hash at the include.
function slug(s) { return String(s).toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, ""); }
function clamp(n, lo, hi) { return n < lo ? lo : (n > hi ? hi : n); }
({ slug: slug, clamp: clamp, version: "1.4.2" })
