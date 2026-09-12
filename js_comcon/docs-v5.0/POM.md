# POM — the Program Object Model (v5.0)

*Companion to `FOUNDATION.md` §3. v4 framing: the POM is **one instance of the
governed-language pattern** (FOUNDATION §2a) — the instance whose grammar is
JavaScript; COM is the sibling instance whose grammar is the config language, and whose
JSON snapshots are, in kernel terms, quotations of COM subtrees. A reflective tree, 1:1
with its source, reached only through capabilities, with mutations classified by safety
class. This document specifies the
node interface, the mutation classes, and the lifecycle. Design stance: mirror the COM's
proven idioms deliberately — same lazy-wrapper pattern, same `describe()` registry, same
tombstone discipline, same broadcast transport, same COW distribution.*

---

## 1. The mirror table — COM idiom → POM equivalent

| js_com (config) | POM (program) | note |
|---|---|---|
| `nginx.http.servers[]`, `srv.locations[]` | `node.children[]`, typed by `kind` | same lazy child-array idiom |
| `locations.find(…)`, `nginx.http.match(uri)` | `node.query(selector)` | selector = the target sub-language — one language for policies *and* interactive use, no dialect |
| property read/write on wrapped C structs | `text()/quote()` reads; `replace/insert/remove` writes | **all reads return quotations** (cap-free; SEMANTICS §2 REFLECT) |
| `nginx.describe()` — op registry + safety classes | `node.describe()` — per-**kind** registry + classes | same **describe ⊇ mutable** drift rule |
| `addLocation()/removeLocation()` + tombstones | `insertBefore/After(q)`, `remove()/revive()` + tombstones | carry the tombstone+revive lesson (the Track-L bug class) |
| `clone({depth})` | `quote()` | a cap-free copy *is* the clone |
| cfgbus broadcast (demo A2.8) | epoch broadcast for live rewrite | same transport, new payload |
| snapshot/rollback (demo A2.7) | binding/tree **epochs**, snapshotable | administrative rebind = new epoch |
| config tree built in master, COW-shared to workers | POM built at stage 0 in master, COW-shared | same memory model as the JS runtime itself |

---

## 2. The node interface

A program never touches "the POM" — only a **handle-scoped view**: `h.root`, where
`reach(h)` bounds visibility and `ops(h)` bounds capability. Everything below is
implicitly gated by the handle.

```js
NodeView {
  // -- identity & shape (op: read) ---------------------------------------
  kind        // 'module' | 'class' | 'function' | 'block' | 'stmt' | 'expr'
  id          // stable PATH-based id (survives edits elsewhere in the tree)
  hash        // content hash of the subtree (changes on any edit)
  span        // {file, line0, col0, line1, col1} — the 1:1 source mapping
  parent, children[], name?, anchors[]   // anchors: queryable ATTRIBUTES, not nodes

  // -- reads: always quotations (op: read) --------------------------------
  text()      // source text        → quotation
  quote()     // structured subtree → quotation   (subsumes COM clone)
  query(sel)  // target-language selector → NodeView[]

  // -- introspection (op: read / inspect-binding / read-lowering) ---------
  describe()  // per-kind ops, writability, safety class — mirrors nginx.describe()
  binding     // {epoch, profile, onViolation, names[]} — REDACTED view by default
  lowered     // {bc?, c?, so?} provenance links + dirty flag

  // -- mutations: admitted quotations only (op: rewrite; classified) ------
  replace(q)  insertBefore(q)  insertAfter(q)  remove()  revive()

  // -- binding (kernel ops via the handle) ---------------------------------
  bind(env, opts)      // meet — monotone down; any bind-capable handle
  rebind(env, opts)    // administrative: NEW EPOCH — op: admin only
}
```

**Identity rule (fragment identity / supply chain):** `id` is **creation-ordered**
(a monotonic id assigned at first admission), **never positional** *(v5.0 — R8:
positional paths silently retarget policies when `insertBefore` shifts siblings)*.
*(v5.3 — C1, persistence:)* creation-ordered ids are **assigned once and recorded in
the canonical config tree** (representation C — the id map is part of the fragment's
on-disk manifest), drawn from a **persisted monotonic counter**; restart and reload
**load** recorded ids, never re-derive them — otherwise a reload would reassign
parse-order ids and silently retarget every id-bound policy, the exact bug R8 fixed.
This is the identity half of the config-tree ⇄ live-tree correspondence question
(FOUNDATION §13.8);
`hash` is content-stable. Policies **target** by id/selector but **pin** by hash — a
policy pinned to `F@hash₁` refuses to govern a silently edited `F@hash₂`; re-admission
is required, and *(v5.0 — R7)* the refusal applies to the **new epoch only**: the
previously admitted epoch keeps serving; **a bound node is never unbound**, and
executing an unbound node is an explicit error (SEMANTICS, EXEC precondition).
Positional selectors remain available but are flagged **volatile**; every admission
reports **binding-set drift** ("this edit changes what policy P governs: +2/−1 —
acknowledgement required").

**The born-bound rule *(v5.0 — R9)*:** query-bound policies are **live**, not
snapshots — match-sets are recomputed at every admission within the covered reach, so
a newly admitted node that matches an existing query (a fresh `fetch` call site under
a hardened subtree) **enters the world already governed**. Since admission is the only
way code changes, recomputing at admission is both sufficient and cheap; a one-time
snapshot semantics would let new code silently escape existing policies.

**Redaction defaults:** `binding` exposes `epoch + profile` only; `names[]`/environment
detail requires the `inspect-binding` op. Source visibility to any viewer — including a
code-generating AI — is precisely the presence and attenuation of a read-capable handle
(`mediate`'s redact flavor: interfaces visible, bodies hidden).

---

## 3. Mutation safety classes — COM's taxonomy, generalized to code

Every mutable operation in `describe()` carries one of four classes:

- **R — read.** `text/quote/query/describe`. Always safe; gated only by handle
  reach/redaction.
- **L — local/init.** Mutations at stage 0 (master, pre-fork) or on fragments not yet
  admitted/bound — the analog of config parse-time edits. No coordination needed.
- **F — fan-out-required.** Live rewrite / bind / epoch switch on admitted, running
  code in a multi-worker server. Must propagate coherently: broadcast (cfgbus
  transport) → each worker marks the node dirty → execution falls back to the node's
  **bytecode** (the maxim phase-34 hybrid `.so` = C fn + bytecode fallback) → re-AOT →
  all workers switch epoch together. A half-propagated rewrite is exactly the failure
  this class exists to prevent.
- **X — irreversible/guarded.** `remove()` without tombstone, revoking an admission
  certificate, permanently redacting source of a compiled-only node. Requires a guard:
  snapshot-first, explicit confirmation, or reject — COM class-3 semantics.

The **describe ⊇ mutable** invariant carries over verbatim: any op that can mutate must
appear in `describe()` with its class.

---

## 4. Node lifecycle

```
parsed ──admit(K)──▶ certified ──bind(ρ)──▶ bound(epoch e) ──lower──▶ live(.so)
   ▲                                            │                       │
   └───── revive ◀── tombstoned ◀── remove ─────┘        rewrite ──▶ dirty
                                                              │
                                        (bytecode fallback) ──┴── re-AOT ──▶ live(e+1)
```

Live rewrite is therefore never stop-the-world: class-F propagation rides the hybrid
fallback, and the previous epoch remains rollback-able (the A2.7 snapshot pattern)
until retired.

---

## 5. Implementation reuse (why this is cheaper than it looks)

- **Wrap pattern:** the COM's proto-per-call lazy wrapper applies unchanged — and node
  views **must** be lazy (expression-level nodes number in the millions; instantiate on
  query/navigation only).
- **Registry:** `ngx_js_com_describe.c`'s table-driven registry extends with node-kind
  rows rather than being reinvented.
- **Fan-out:** the A2.8 cfgbus broadcast and the SharedWorker channel machinery are the
  class-F transport as-is.
- **COW:** the POM is built in master `init_conf` and COW-shared to workers —
  identically to the JS runtime and config tree today.

---

## 6. Open questions

1. **Granularity floor** — *(resolved, increment D0, 2026-09-03)* materialize **module +
   function** directly from the bytecode tree (each `JSFunctionBytecode` carries its own
   `debug.source` slice, `pc2line`, and nested-function cpool constants — no parser); **block**
   is a lexical scope *inside* a function's bytecode, not a child bytecode object, so it is
   enumerated but synthesized later (from scope opcodes); statement/expression views await the
   full CST (D5). This is the lean answer, confirmed feasible on vendored QuickJS.
2. **Selector-language grammar** — *(resolved, increment D2, 2026-09-03)* a value-level DSL
   over the NodeView subtree (interpreted by a library function under the handle — no new host
   grammar): `selector := term ('within' term)*` · `term := factor+` (AND) · `factor := 'module'
   | 'function' | '*' | 'name(' glob ')'` · `glob := exact | pre* | *suf | *mid* | *`. `A within
   B` selects nodes matching A that have an ANCESTOR matching B (intensional composition — the
   hardening pattern). Matches over self + descendants; recomputed live per call (born-bound, R9).
   Same language for policy targeting and interactive/LSP use, as required. Finer selectors
   *(extended in increment D5b-2, 2026-09-12, over a `cst()` view)*: `factor` also takes
   `'block' | 'stmt' | 'expr' | 'call(' glob ')' | 'type(' glob ')' | 'anchors(' glob ')' |
   'line(' N ')' | 'line(' N '-' M ')'`; a glob may be quoted or bare (`anchors('checkout')` =
   `anchors(checkout)`). `line()` is a span predicate on the node's START line, node-local and
   1-based. **Prefer `anchors()` to `line()` for anything durable** — an edit above the site moves
   the line and moves it silently, which is precisely why FOUNDATION's inline binding is an
   anchor. `anchors()` THROWS on the bytecode tier rather than returning no matches: that tier
   does not parse, so it cannot answer, and "no sites" would be the wrong answer to give a
   hardening query.
3. **Cross-file provenance** — *(resolved, increment D5b-4, 2026-09-12)* **a span now says
   which base it counts in.** A bytecode-tier span is FILE-relative and carries `base:'file'`,
   `file` and `col0`; a `cst()` span is NODE-local and carries `base:'node'` and no file, so it
   cannot be misread as absolute. `node.origin()` converts node-local → absolute against the
   origin the view inherited (`{file, line0, col0}` from the bytecode node it came from, or an
   explicit `comcon.cst(source, {file, line0, col0, offset})` for text you hold rather than a
   live function).

   This was a real defect, not a formality: `pom(fn).line0` was **18** and
   `pom(fn).cst().line0` was **1** for the same function, with no file named anywhere — the
   same field meaning two things at two tiers, so a denial record built from one and read as
   the other points at the wrong place and looks right.

   `origin()` returns **null** when the origin is unknown, and an absolute `range` only when a
   byte offset was supplied (the bytecode tier has none — `pc2line` maps lines, not offsets).
   Inventing a plausible location is the source-map lie; a denial record naming the wrong
   file:line is worse than one that says it does not know.

   **The include hop:** a fragment's synthetic file origin is `<comcon-fragment>`
   (`NGX_JS_COMCON_FRAGMENT_ORIGIN`), and a fragment failure now reports
   `... at <comcon-fragment>:LINE:COL` — only that token, never the rest of the stack, which
   also names host frames. The line is the AUTHOR's line because `include()`'s wrapper preamble
   contains no newline; that is a **contract** (add one and every reported line shifts by one,
   silently), pinned by `t/comcon_pom_origin.t`. And `pom()` now **refuses** a confined
   fragment's bound wrapper: it used to describe the wrapper — a two-line closure in
   `<comcon-bootstrap>` — and answer every query about it.

   *Source-rewrite hardening resolved at D5b-3 (2026-09-12):*
   `comcon.harden(node, query, wrapper)` rewrites every matched site (`$$` = the site's own
   source) and returns a report whose `quotation` installs through D4 — the rewrite produces
   TEXT and never an install, so it can be reviewed, diffed and admitted first. Node-local
   spans are what make this work: the offsets a query reports are exactly the offsets the
   splice uses, and sites are spliced back-to-front so each range still indexes the text it
   was measured in. `comcon.cst(source)` gives the same view over plain text, for code you
   have as source rather than as a live function.
4. **p_symbol enumeration** — *(resolved, increment D0)* schema **`comcon-pom-1`** (never
   renumber, only append): `module=1, function=2, block=3, stmt=4, expr=5` (`quickjs.c`,
   `NGX_COMCON_POM_*`). Kinds 1–2 are materialized in D0; 3–5 are reserved for later synthesis.
   Stable, versioned naming of node kinds across engine upgrades (carried from v2 §10.2).
