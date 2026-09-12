# Vendored: acorn 8.14.0

**Why this is here.** D5b-2 needs a real ES parser to build the
expression-granularity CST that D5b-3's source-rewrite hardening consumes.
`INCREMENT_D.md` §"The crux — the parser" chose *vendor a proven parser* over
hand-rolling one, on the grounds that the parser is **security-relevant TCB** —
a mis-parse that admits something it should not is a soundness hole, so
proven-ness beats hand-rolled here.

| | |
|---|---|
| package | `acorn@8.14.0` (npm) |
| file | `dist/acorn.js` (UMD build) |
| licence | MIT — see `LICENSE.acorn` |
| tarball sha256 | `04c1f5545e4e9140e288bb56b4cbbc4ffd730213e6331330e2bcefc649462104` |
| `acorn.js` sha256 | `bec194b9abb10147d3bb77e544d95cf1c7b4f9f42dad00dfc83791909ebf49c7` |
| vendored | 2026-09-12 |

## Re-fetch and verify

```bash
npm pack acorn@8.14.0
sha256sum acorn-8.14.0.tgz          # must equal the tarball hash above
tar xzf acorn-8.14.0.tgz
sha256sum package/dist/acorn.js     # must equal the acorn.js hash above
diff package/dist/acorn.js src/js/vendor/acorn.js
```

`t/comcon_parser_vendor.t` checks the embedded copy against this file, so a
silent edit to the vendored source fails the suite.

## The TCB rules this artifact lives under

1. **Fail closed.** A parse error must REJECT, never admit. Verified by test.
2. **Never re-print.** Rewrites (D5b-3) splice at byte offsets into the ORIGINAL
   source. acorn's output is used for *spans*, never to regenerate code — so no
   code generator enters the TCB, and comments/formatting round-trip exactly.
3. **Host-side only.** It runs as trusted analysis over untrusted TEXT. It is
   never exposed to a confined fragment, and it never evaluates what it parses.
4. **Pinned.** Upgrading is a deliberate act: re-run the verification above and
   re-read the diff, because this is TCB.
