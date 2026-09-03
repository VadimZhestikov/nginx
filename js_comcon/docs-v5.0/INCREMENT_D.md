# INCREMENT — D: POM nodes (the reflective program tree) — scoping

**Status:** 🚧 IN PROGRESS (2026-09-03, docs at v5.41). **D0 ✅** (substrate + p_symbol
enumeration), **D1 ✅** (lazy read-only NodeView), **D2 ✅** (`query(sel)` selectors); D3–D5
pending. Follows the operator kernel
(`INCREMENT_MCFG.md`), the convergence (`INCREMENT_CONVERGE.md`), and the closure/quotation
resolution (`bind` v5.37, `realize`/`quote` v5.38). This is the last standing forward frontier on
the confinement track; the alternative track is maxim → test262 (the untrusted-native gate).

## 1. Goal

Give COMCON a **Program Object Model** — a reflective tree over tenant fragments, 1:1 with source,
reached only through capabilities, with mutations classified by safety class (`POM.md`, FOUNDATION
§3). Everything shipped so far operates on **compiled `JSValue` functions** (free-name/dynamic-code
introspection); the POM is the missing *structured* surface that unlocks four deferred capabilities
at once:

1. **POM-node quotations** — `quote()` of a *parsed subtree* (not just a whole source string) +
   structured `${…}` splices as cap-free data leaves → `realize` over a real node.
2. **`query(sel)` targeting** — extensional (this node/anchor) and intensional ("all functions
   named X in module M"; ultimately "all `fetch` call sites under `vendor/**`").
3. **`includeAt`** (anchor-splice + query-targeting) — subsumed here; not a separate operator
   ([[feedback-reuse-jscom-primitive]]).
4. **Live rewrite / epochs** — `replace/insert/remove/revive` + tombstones, class-F fan-out,
   epoch broadcast, rollback (the A2.7 snapshot pattern as POM epochs).

**Design stance (POM.md §5, unchanged):** mirror the COM's proven idioms deliberately — same lazy
proto-per-call wrapper, same `describe()` table-driven registry, same tombstone+revive discipline
(the Track-L bug class), same cfgbus broadcast transport, same master-`init_conf`→COW distribution.
This is why it is cheaper than it looks: we are extending machinery, not inventing it.

## 2. The crux: where the parse tree comes from

QuickJS compiles source → bytecode **single-pass**; there is **no persistent AST**. But the
vendored engine already gives us, per compiled function (`JSFunctionBytecode`, `quickjs.c`):

- `has_debug` + `debug.source` — **the function's own source text** (span slice);
- `debug.pc2line_buf` — pc→line table (start line; column-precise spans need more);
- nested-function constants in the cpool — **the function/module child tree**;
- and our own precedent helpers `js_comcon_collect_free_globals` / `js_comcon_uses_dynamic_code`
  (`quickjs.c:24331+`, exported via `quickjs.h`) already walk exactly these structures.

**Consequence — the granularity floor (POM.md §6 Q1, "the lean answer"):** materialize only
**module / function / block** nodes as first-class C-side nodes, built from bytecode introspection
(new `js_comcon_pom_*` helpers in the same pattern), and **synthesize statement/expression views on
demand** later. This delivers navigation, function-level `quote()`, function/module `query()`, and
live rebind/epochs **with no CST parser**. A full statement/expression CST (columns, trivia,
call-site selectors) requires a real parser — the "M3 front-end" — and is a **separable, gated**
phase (D5) that carries the marquee *intensional hardening* showcase (§38, "callsites(fetch)").

> **Substrate recommendation:** coarse POM from vendored-QuickJS bytecode introspection for
> D1–D4; defer the full-CST front-end (and with it statement/expression granularity + the
> callsite-hardening scenario) to a separately-gated D5. Do **not** couple the base POM to maxim's
> front-end (that would tie the interpreted-tier POM to the JIT tier).

## 3. Phases

Each phase is independently shippable, gated on SR-2 + full regression, and preserves the
security-critical POM invariants (§4).

- **D0 — substrate + `p_symbol` enumeration (GATE).** ✅ DONE (2026-09-03). Granularity floor
  resolved: **module + function** materialized directly from the bytecode tree (no parser); block
  is an intra-function scope (enumerated, synthesized later); stmt/expr await the CST (D5).
  `p_symbol` enumeration resolved: schema **`comcon-pom-1`** (`module=1, function=2, block=3,
  stmt=4, expr=5`; never renumber, only append) in `quickjs.c` (`NGX_COMCON_POM_*`). Added
  `js_comcon_pom_inspect` (quickjs.c, beside the free-globals/dynamic-code helpers) — reflects a
  compiled fragment as a node tree {kind,name,line0,line1,sourceLen,hash,childCount,children[]},
  content hash = FNV-1a over the source slice (R7 pin). Diagnostic bridge `comcon.__pomInspect`
  (host, internal); **no lazy NodeView surface yet** — that is D1. `t/comcon_pom_substrate.t`
  (14): kinds, child tree, spans, hash stability (identical source ⇒ identical hash; changed ⇒
  different), native-fn ⇒ undefined. **Gate met:** kinds enumerated, spans + hashes stable across
  rebuilds.

- **D1 — coarse read-only POM.** ✅ DONE (2026-09-03). `comcon.pom(fragment)` → a lazy `NodeView`
  tree: `kind / id / hash / span / line0 / line1 / childCount / name`, lazy `children` / `parent`
  getters, methods `text() / quote() / describe()`, redacted `binding`; the node is **frozen**
  (immutable view). **Reads return quotations** — `text()`/`quote()` hand back v5.38 `comcon.quote()`
  values (cap-free), never raw source (SEMANTICS REFLECT). `describe()` emits node-kind read-op rows
  with safety class `R` (describe ⊇ mutable; mutations arrive in D4). Identity: **creation-ordered
  ids**, stable within a process (keyed by content hash + path); **content hashes** for pin-by-hash
  (R7). One new C accessor `js_comcon_pom_node_at` (single node at a path) backs it; **GC-safe** — it
  holds no pointers and the root fragment is kept alive by the JS closure, so navigation is flat
  (`t_stress/com_pom_navigate.t`, 5000×). `t/comcon_pom_nodeview.t` (16). **Gate met:** navigate a
  real fragment tree, `quote()` a real subtree, leak flat. *(Deferred to later phases: cross-restart
  id **persistence** in the canonical config tree — rides admission/config-tree integration; true
  proto-per-call laziness for the millions of **expression** nodes — only needed at D5 granularity;
  `reach(h)`/`ops(h)` handle attenuation — arrives with mediate-flavor redaction hooks.)

- **D2 — `query(sel)` selectors.** ✅ DONE (2026-09-03). `node.query(sel)` — a value-level DSL over
  the NodeView subtree (interpreted under the handle; no new host grammar): `selector := term
  ('within' term)*` · `term := factor+` (AND) · `factor := 'module' | 'function' | '*' | 'name('
  glob ')'` · `glob := exact | pre* | *suf | *mid* | *`. `A within B` = nodes matching A with an
  ANCESTOR matching B (intensional composition — the hardening pattern). Matches self + descendants;
  results are NodeViews (directly `quote()`-able/navigable). **Born-bound (R9):** query is a pure
  function recomputed live per call — never a snapshot; D4 will call it at admission time so newly
  admitted nodes enter already governed. Malformed selectors throw. Pure-JS (bootstrap), no engine
  change. `t/comcon_pom_query.t` (13). Spec: POM.md §6 Q2 resolved. **Gate met:** extensional +
  coarse-intensional selection, live recompute. *(Finer selectors — `callsites(fetch)`, span/anchor
  predicates — extend the grammar at D5 when stmt/expr nodes exist.)*

- **D3 — POM-node quotations + structured splices.** `quote(node)` of a parsed subtree; `${…}`
  splices attached as **stone** (deep-frozen, cap-free) data leaves — structurally injection-immune
  (SEMANTICS §4.4). Marries to shipped `realize`/`quote`; unlocks `includeAt` **anchor-splice**.
  **Gate:** realize a spliced subtree; a spliced capability is a stage-0 error at the producer.

- **D4 — mutations + epochs (class-F).** `replace/insert(Before|After)/remove/revive` + tombstones
  (carry the Track-L revive lesson); admitted-quotations-only writes; **epoch broadcast** over the
  A2.8 cfgbus transport; bytecode-fallback during propagation → re-AOT → coherent epoch switch;
  rollback = prior epoch retained (A2.7). Class-X guards (`remove` w/o tombstone) require snapshot/
  confirm. **Gate:** live rewrite is never stop-the-world; reload-leak + fan-out tests flat.

- **D5 — full-CST front-end (SEPARATELY GATED, optional).** A real JS parser producing an
  ESTree-like CST with column-precise spans + trivia → statement/expression granularity →
  **intensional call-site selectors** (`callsites(fetch) within module('vendor/**')`) → the
  brownfield-hardening showcase (§38). Heaviest component; do not start before D1–D4 prove the
  model. Provenance hop for `include`-spliced fragments (POM.md §6 Q3) lands here.

## 4. Invariants to preserve (non-negotiable)

- **Reads return quotations** — cap-free by construction; source visibility = presence of a
  read-capable handle (redact flavor: interfaces visible, bodies hidden).
- **describe ⊇ mutable** — every mutating op appears in `describe()` with its safety class (R/L/F/X).
- **Tombstone + revive** — never positional retarget; ids creation-ordered + persisted (R8/R9).
- **Pin-by-hash refusal is new-epoch-only** — a bound node is never unbound; the prior epoch keeps
  serving (R7).
- **Born-bound** — query-bound policies recompute their match-set at every admission within reach.
- **Confinement unchanged** — the POM is a host-side reflective surface; a *tenant*'s POM handle is
  attenuated by `reach(h)`/`ops(h)`; the A1 reach gate and both-tier erasure are untouched.

## 5. What this does NOT include

Adaptive profiles (transform mediation, ≈M9); the `rateLimit`/`transform`/`audit` mediate flavors;
`policy({...})` as a reified value; WASM ingestion; the M2 typed host-API schema. These remain
design and are orthogonal to the POM.

## 6. Recommended entry

Start at **D0** (substrate + p_symbol enumeration) — it is the gate that de-risks everything after
it, is small, and produces a decision note plus the C-side node accessor without committing to the
JS surface. D1 follows directly on a green D0.
