# INCREMENT — D: POM nodes (the reflective program tree) — scoping

**Status:** 🚧 IN PROGRESS (2026-09-03, docs at v5.43). **D0 ✅** (substrate + p_symbol
enumeration), **D1 ✅** (lazy read-only NodeView), **D2 ✅** (`query(sel)` selectors), **D3 ✅**
(POM-node quotations + stone splices), **D4a ✅** (epochs + admitted replace + rollback), **D4b ✅**
(class-F multi-worker fan-out), **D5a ✅** (call-site audit), **D5b-1 ✅** (declarative-profile
checker), **D5b-2 ✅** (full CST + finer selectors + anchors, 2026-09-12), **D5b-3 ✅**
(source-rewrite hardening), **D5b-4 ✅** (cross-file provenance), **D4c ✅** (compiled tier
under a live epoch switch, all 2026-09-12). **INCREMENT D IS COMPLETE.** Follows the operator kernel
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

- **D3 — POM-node quotations + stone splices.** ✅ DONE (2026-09-03). `comcon.quote(source,
  splices?)` accepts producer **splices** — deep-checked **stone** (cap-free: no functions,
  capabilities, or accessors; a violation is a stage-0 error at the producer). `realize` binds each
  splice as a **JSON literal in an enclosing IIFE var**, so the quoted code resolves the splice name
  to escaped *data* — a spliced string can never smuggle code (JSON.stringify escaping = the
  parameterized-SQL defense), and the splice names become bound closure vars (invisible to the admit
  free-name gate). This is the data-plane of §4.4's two-phase binding on the source substrate; the
  structured POM-node splice (into a *parsed subtree*, preserving sub-node handles) awaits stmt/expr
  nodes (D5). A POM node's own `quote()` (D1) is now **realizable** — `realize(node.quote(), K, env)`
  compiles + runs a real subtree, marrying the POM to the operator kernel. Pure-JS, no engine change.
  `t/comcon_pom_splice.t` (9). **Gate met:** realize a spliced quotation (scalar + record), injection
  neutralized, a spliced capability/function/getter refused at the producer, realize over a
  `node.quote()`. *(`includeAt` anchor-**splice** — inserting a fragment at a named anchor **site** —
  is a tree MUTATION and lands with D4, not here; D3 delivers the splice-value machinery.)*

- **D4 — mutations + epochs (class-F).** 📐 SCOPED (2026-09-03); see the detailed sub-scope in §7.
  In one line: POM mutation is **rebuild-on-write** (recompile an admitted quotation, swap the live
  binding site, new epoch) — *not* an in-place bytecode edit, which QuickJS does not allow — and the
  transport/tombstone/fan-out/rollback machinery **already exists in js_com** (reuse fundament), so
  D4 is a thin epoch+mutation-discipline layer, staged D4a (epochs + admitted replace + rollback),
  D4b (class-F multi-worker fan-out over cfgbus), D4c (deferred: compiled-tier live re-AOT).

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

## 7. D4 sub-scope — mutations + epochs (rebuild-on-write)

**The model.** Our POM (D0–D3) reflects compiled `JSFunctionBytecode`; QuickJS bytecode is not
editable in place, so a POM "mutation" cannot mean rewriting a function body. It means **rebuild-on-
write**: `replace(quotation)` recompiles the admitted quotation (via `realize`/`include`) into a
**new bound fragment** and swaps it into the live **binding site**, as a **new epoch**; the prior
epoch is retained for rollback. This is exactly POM.md §4's lifecycle
(`parsed→certified→bound(epoch e)→live`; `rewrite→dirty→re-AOT→live(e+1)`) and the "config is a
program; a rewrite is a new bound version" thesis.

**The binding site is the existing COM setter** ([[pilgrim-shell-fundament-principle]]): a bound
POM fragment is one installed at `location.handler`. So a POM mutation = *recompile the quotation +
reassign the handler + bump the epoch*.

**Reuse finding (the load-bearing point).** Nearly every mechanism D4 needs already exists in
js_com — D4 orchestrates them, it does not reinvent them:

| D4 needs | Already in js_com | 
|---|---|
| install/replace a live fragment | `location.handler` setter (`__ngx_handlers__`, `handler_idx`) |
| tombstone + revive (Track-L lesson) | `removeLocation`/`reviveLocation`, `removeServer`/`reviveServer` + tombstone arrays |
| class-F multi-worker fan-out | cfgbus broadcast + SharedWorker channels (demos A2.2 / A2.6 / A2.8) |
| snapshot / rollback (epochs) | admin snapshot/rollback (demo A2.7) |

So D4's **genuine addition** is a thin layer: route writes through the **admitted-quotation**
discipline, track **epochs** (monotonic per site) with **rollback history**, classify each op by
**safety class (R/L/F/X)** in `describe()`, and recompute **born-bound** queries (R9) after a change.
Likely **little or no new C** — a JS orchestration over `realize`/`include` + the COM setters.

**Stages.**

- **D4a — epochs + admitted replace + rollback (single-worker semantics).** ✅ DONE (2026-09-03).
  `comcon.bindAt(site, quotation, contract)` → realize + install at the site + return a frozen epoch
  handle. `site` is an `install(callable, epoch)` fn the caller wires to `loc.handler = …` (no
  parallel install path — the shell fundament). Ops: `replace(q)` (admit → realize → install →
  epoch++, retain prior; class F), `rollback()` (restore the prior epoch exactly), `remove()`
  (tombstone via the caller's site; class X), `revive()`, `call(arg)`, `epoch()`; `describe()` lists
  them with R/L/F/X classes. Rollback history is **bounded** (BINDCAP=8) and a superseded fragment
  beyond the window is **freed** via a new `comcon.__freeConfined(handle)` C path (invoking a freed
  handle then errors, no crash) — so live rewrite does **not** accumulate compiled fragments.
  `t/comcon_pom_mutate.t` (12): live rewrite (v1→v2), exact rollback, tombstone+revive, describe
  classes, and 500 replace cycles flat (<32 KB). **Fixed a pre-existing latent bug** the request-time
  `replace` exposed: `comcon_frags` was created (lazily, first include) on the config-eval cycle pool
  and grew at request time on that now-stale pool → SIGSEGV; it now owns a dedicated long-lived pool
  (`comcon_frags_pool`, destroyed at teardown). **Gate met.**

- **D4b — class-F multi-worker fan-out.** ✅ DONE (2026-09-03). `comcon.bindShared(key, quotation,
  contract, onRequest)` — the multi-worker spelling of `bindAt`. The current `{epoch, source}` is the
  single source of truth in **`nginx.shared`** (lock-free, instantly visible to every worker), and
  each worker's `h.handler(req)` **reconciles lazily**: on each request it reads the shared epoch
  and, if newer than its locally compiled one, recompiles the shared source **in its own
  compartment** and swaps (rebuild-on-write per worker), freeing the old fragment. So a `replace()`
  in any one worker fans out to **all** of them coherently — no worker ever serves a torn state, and
  only the source string crosses (never a JSValue). Chose **lazy pull** (shared KV as truth +
  per-request reconcile) over eager push: strictly coherent, simpler, and pays a recompile only on
  the first request of a new epoch per worker. Reuses `nginx.shared` as the transport — **no new
  broadcast mechanism** (the shell fundament). `t/comcon_pom_fanout.t` (4 workers): before replace
  every worker serves v1; after ONE replace every worker serves v2 at epoch 1. **Gotcha fixed:**
  `nginx.shared` is unavailable at config-eval time, so all shared access is deferred to request time
  (lazy seed in `reconcile`). **Gate met:** rebind propagates to all workers; no stop-the-world.

- **D4c ✅ (2026-09-12) — the compiled tier under a live epoch switch.** Scoped by measurement
  rather than by the plan's wording, because the measurement changed the answer.

  **The safety half holds and is now tested:** a rewrite of a fragment that WAS lowered to
  native C is answered by the new epoch — never by the old `.so` — and rollback restores the
  retained epoch (`t/comcon_aot_epoch.t`).

  **The re-AOT half is not a worker's to do.** A live epoch switch runs in a worker, post-fork,
  and the gcc thread does not survive `fork()` ([[jit-inert-in-nginx-workers]]), so the new
  epoch runs the **bytecode fallback**. That is correct and coherent; it is just not native.
  Doing better needs a compiler-bearing process (the master still has its thread) to build the
  `.so` and workers to pick it up from the hash-keyed JIT cache — new IPC, a separate increment.

  **What was actually missing was the ability to tell.** `js_comcon_aot_compile()` returns 0 for
  any bytecode function — "eligible", never "compiled", as its own header says — and the include
  site logged *"include fragment lowered to native C"* on that 0, on **every** include, including
  every request-time epoch switch. So the tier was unobservable and the log was wrong.
  `comcon.aotStatus(fragment)` → `{jit, functions, compiled}` answers it (read-only: asking never
  compiles), and the notice now says `NATIVE (n of m functions)` or
  `BYTECODE (m functions, nothing lowered: no compiler in this process)` — two messages with no
  shared substring, so grepping for one cannot match the other.

**D4a surface (RESOLVED 2026-09-03):** a **thin `comcon.bindAt(site, quotation, contract)` handle**
— it returns an epoch handle carrying `replace`/`rollback`/`remove`/`revive` and the rollback
history, but `install` is implemented as *exactly* `loc.handler = …` (the existing setter), so there
is **no parallel install path** and the shell fundament holds. The handle is the natural home for
epoch + history state; the actual live mutation rides the COM setter verbatim.

**Invariants (from §4) that bite here:** admitted-quotations-only writes; tombstone never positional
(R8); pin-by-hash refusal is **new-epoch-only**, a bound node is never unbound (R7); born-bound
recompute at the change (R9); class-X guard on `remove` without tombstone.

## 8. D5 sub-scope — statement/expression granularity (the CST frontier)

**What D5 is for.** The one capability D0–D4 cannot reach: **intensional call-site hardening**
(SHOWCASE §38) — attach a policy to *code you do not own* by query ("every `fetch` call site under
`vendor/**`"), born-bound. This needs statement/expression nodes with column-precise spans, which
need a real parser (QuickJS retains no AST; D0–D4 stop at function granularity from the bytecode
tree).

**Reuse finding (reframes D5's value — decide before building).** Most of §38's *enforcement* is
**already delivered by the capability kernel**: a fragment's `fetch(...)` resolves `fetch` as a free
name, bound at realization through the manifest — so "harden every use of `fetch`" is just
`grant(env, "fetch", mediate(fetchCap, guard))` / a mediated import (v5.29 `mediate` + D3 realize).
The fragment cannot reach an un-granted `fetch`, and a granted one is already attenuated. So the
**common case is ours today, with zero new machinery.** D5's genuine residual is narrower:

1. **positional / per-site** hardening (harden *some* call sites, not the name globally);
2. **method-call** sites (`obj.fetch(...)` — a property call, not a free name);
3. targets **bound locally** inside the fragment (not via the manifest);
4. **source-level rewrite** of arbitrary full-language third-party code.

These are advanced/rare relative to the free-name case the kernel already covers.

**Substrate decision (unchanged from §2).** A CST requires a **JS-side parser** — the "M3
front-end" as a policy-JS component — *not* engine surgery and *not* coupling to maxim's front-end.
A full ES parser is a large, error-prone build; that cost is the reason D5b is gated separately.

**Stages.**

- **D5a — call-site ENUMERATION from bytecode (no parser).** ✅ DONE (2026-09-03).
  `node.references(name)` enumerates every reference to a free name or method `name` in a fragment
  (whole subtree) with line numbers; `node.callsites(name)` is the subset that are actual **call
  sites**. Callee↔call correlation is **exact**: `js_comcon_pom_callsites` (quickjs.c) tracks the
  operand stack (per-opcode `n_pop`/`n_push`, variadic argc for `call*`), so a nested-argument call
  like `fetch(helper(2))` is still attributed to `fetch`. Two reference kinds: free name
  (`OP_get_var`/`get_var_ref` → `closure_var[idx].var_name`) and method (`OP_get_field`/`get_field2`
  atom); each record `{name, line, method, call}`. Locally-bound callees (`OP_get_loc`) are out of
  scope — they need the CST (D5b). This is the intensional **audit READ side** of §38; the
  **enforcement side is the capability kernel** (mediate a granted name), so "audit + enforce" is
  complete without a parser. `t/comcon_pom_callsites.t` (11). **Gate met.** *(Gotcha: an engine-only
  edit needs a forced relink — `rm objs/nginx` — since the nginx Makefile doesn't track
  `libquickjs.a`; see [[build-and-test]].)*

- **D5b — full CST front-end + source-rewrite hardening (SEPARATELY GATED; a milestone, not a bite).**
  A JS-side ES(-subset) parser producing stmt/expr nodes with column spans → `node.children` at
  expression granularity → `harden(node, "callsites(x)", wrapperQuotation)` rewrites matched sites at
  the source level and rebuilds via D4. This is the real M3 front-end and the only path to residuals
  1–4 above. Large; do not start without an explicit decision that the residual value justifies a
  parser. Cross-file provenance (POM.md §6 Q3) lands here too. **Also folded in here (2026-09-03):**
  the one genuine platform hook the config-language pattern needs — a **sound declarative-profile
  checker** (`syntax_allowed`: no loops/dynamic/computed access) + normalization to diffable
  **descriptor tables** — so an operator can *soundly review/diff an untrusted config proposal*, not
  just validate it at runtime. This is the only part of a config-DSL that is not userland-expressible
  (see `PATTERN_config_language.md`); the CST built here is what makes it checkable.

**Recommendation.** Increment D's **core is complete at D4b** — a reflective POM with selectors,
quotations+splices, and coherent live rewrite across workers. **D5a** is a small, high-value add
(intensional audit). **D5b** is a genuinely separate milestone whose enforcement value is largely
pre-empted by the capability kernel; schedule it only if positional/source-rewrite hardening becomes
a concrete requirement. The other open branch is maxim → test262 (the untrusted-native gate),
independent of the POM.

**Decision (2026-09-03):** proceed with **D5a** (bytecode call-site enumeration / audit); D5b stays a
separately-gated future milestone, D4c stays deferred.
**Superseded (2026-09-12):** D5b-1…4 and D4c are all done; increment D is complete. The one piece
deliberately NOT built is re-AOT of a live epoch inside a worker, which the fork model forbids —
see D4c above for the shape a future increment would take.

## 9. D5b detailed sub-scope — the CST front-end (2026-09-04)

D5a delivered call-site **audit** from bytecode (no parser); D5b is the parser-backed remainder.
It carries four things, of very different size and value — scoped and staged accordingly.

### What D5b delivers
1. **Declarative-profile checker** (`syntax_allowed` + descriptor-table normal form) — soundly review
   an untrusted config/policy proposal (prove it stays in the loop-free / no-dynamic / no-computed
   subset; normalize to diffable descriptor tables). This is the one platform hook the config-language
   pattern needs (`PATTERN_config_language.md`).
2. **Expression-granularity POM** — a real CST (stmt/expr nodes, column-precise spans, trivia), so
   `node.children` descends below function granularity and `hash`/`id` reach expressions.
3. **Finer selectors** — extend the D2 grammar with `callsites(x)` (as real nodes, not bytecode
   records), method-call sites, span/anchor predicates.
4. **Source-rewrite hardening** — `harden(node, "callsites(x)", wrapperQuotation)` rewrites the matched
   sites at the source level and rebuilds via D4 (rebuild-on-write). The only path to the residuals
   D5a/the kernel cannot reach: positional/per-site hardening, method calls, locally-bound targets,
   and full-language third-party code.
5. **Cross-file provenance** — span mapping through `include`-spliced fragments (POM.md §6 Q3).

### Reuse framing (what is *already* covered — decide before building the big parser)
- **Enforcement of free-name hardening is the capability kernel's** (`grant(env,"fetch",mediate(…))`).
- **Audit ("where is X called") is D5a** (bytecode scan).
So D5b's *unique* value is (1) declarative-proposal analyzability and (4) source-rewrite of the
residual cases. If neither positional/source-rewrite hardening nor full-language third-party hardening
is a concrete requirement, the kernel + D5a already cover the practical surface — build (1) and stop.

### The crux — the parser
A CST needs a JS parser (QuickJS keeps none; engine-patching it is single-pass surgery + couples to
internals; maxim's front-end couples the interpreted tier to the JIT — both rejected as in §2). The
real choice is **build vs vendor** a JS-side parser:
- **Hand-roll a full ES parser** — thousands of lines, subtle-bug-prone; and the parser is
  **security-relevant TCB** (a mis-parse that admits something it shouldn't is a soundness hole), so
  bug-proneness is a *security* cost, not just effort.
- **Vendor a proven parser** (acorn-class, MIT, pure JS → runs host-side as trusted analysis) and map
  its ESTree output to POM nodes. Larger trusted surface, but *proven-ness beats hand-rolled* for a
  TCB soundness component — the doctrine's "verify the finite TCB" favours the battle-tested artifact.
- **The declarative subset (item 1) is tiny** — a small hand-rolled recursive-descent parser for the
  straight-line `syntax_allowed` grammar is low-risk and needs no full parser.

**Recommendation:** small hand-rolled parser for the declarative subset (D5b-1); **vendor** a proven
ES parser for the full CST (D5b-2) rather than hand-roll one.

### Stages
- **D5b-1 — declarative-profile checker + descriptor tables.** ✅ DONE (2026-09-04).
  `comcon.reviewDeclarative(source)` — a small hand-rolled recursive-descent parser (pure JS, in the
  comcon bootstrap; no engine change) for the `syntax_allowed` subset: a straight-line sequence of
  fluent call-chains over dotted name paths, with literal / nested-chain / free-name-ref / obj / arr
  arguments — NO loops, conditionals, operators, assignments, computed access, or functions. A
  **sound rejecter** (parses only that grammar, throws on anything else), returning diffable
  **descriptor tables** (`{declarative:true, statements:[[{op,args}…]…]}`). `realize(q, {profile:
  'declarative'})` runs it before admission and refuses a non-declarative proposal (charged at the
  realizer). This is the config-language pattern's one platform hook (`PATTERN_config_language.md`):
  an untrusted proposal is now *soundly reviewable*, not merely runtime-validated. `t/comcon_declarative.t`
  (16). Independent of the full parser.
- **D5b-2 ✅ (2026-09-12) — full CST + expression-granularity POM + finer selectors.** acorn 8.14.0
  vendored as TCB (`src/js/vendor/`, fail-closed, `t/comcon_parser_vendor.t`); `node.cst()` maps
  ESTree → POM block/stmt/expr nodes with node-local spans (`t/comcon_pom_cst.t`); `query` gained
  `block`/`stmt`/`expr`/`call(glob)`/`type(glob)`/`anchors(glob)`/`line(N)`/`line(N-M)`
  (`t/comcon_pom_anchors.t`). **Anchors are the ROADMAP §4.1 deliverable**, not a selector
  convenience: `"use comcon: <name>";` directive-prologue markers, inert in plain JS, exposed as
  node attributes — so a policy names a SITE and survives edits above it, where `line(N)` does not.
  Both predicates ship together and the test asserts that difference on two fragments identical but
  for two inserted lines. Anchor recognition reuses acorn's own `directive` determination and
  matches the RAW spelling, so the name a reviewer reads is the name that binds.
- **D5b-3 ✅ (2026-09-12) — source-rewrite hardening.** `comcon.harden(node, query, wrapper)`
  replaces every matched site with the wrapper, `$$` standing for the site's own source, and returns
  a REPORT whose `quotation` installs through D4 (`bindAt`/`replace`) — so the rewrite is pure text
  (class R/L) and the authority to install stays in D4, where it can be reviewed and admitted first.
  `comcon.cst(source)` came with it: a CST over plain TEXT (the §38 shape — you have vendor source,
  not a live function), which is also what makes a rewrite reviewable structurally and hardening
  passes composable. Targets the residual: the test hardens a **locally-bound callee**
  (`var g = real; g('a')`) — a call no grant can name and `callsites()` cannot see — and asserts the
  guard STOPS it (the wrapped function's log is empty), against the unhardened fragment in the same
  request. `t/comcon_pom_harden.t` (31, eight negative controls).

  **Limits, stated because they are easy to overread.** The wrapper must be ONE
  `ExpressionStatement` (`$$` is spliced at an expression position), which is what stops
  `__guard($$); evil()` — that splices to three *valid* statements, so re-parsing cannot catch it.
  It is NOT an injection defence in general: a wrapper is code, a comma sequence is one expression,
  and what bounds a wrapper is **the env it is realized under**, never its syntax. Overlapping
  matches are refused rather than half-rewritten. A **host function cannot be granted** into a
  compartment (only C-wrapped COM caps cross), so a wrapper installed on the confined tier must
  carry its own logic or call a granted capability — asserted, along with the live site surviving
  the refused install untouched.
- **D5b-4 ✅ (2026-09-12) — cross-file provenance.** A span says which base it counts in
  (`base:'file'` + `file` + `col0` at the bytecode tier, `base:'node'` at the CST tier) and
  `node.origin()` converts node-local → absolute; null when the origin is unknown, absolute
  `range` only when an offset was supplied. The include hop: `<comcon-fragment>` is the synthetic
  origin (one constant), fragment failures report `at <comcon-fragment>:LINE:COL` (that token
  only — the rest of the stack names host frames), and `pom()` refuses a fragment's bound wrapper
  instead of describing it. `t/comcon_pom_origin.t` (16, nine negative controls).
  **What it fixed:** `pom(fn).line0` = 18 vs `pom(fn).cst().line0` = 1 for the same function, with
  no file anywhere — MANUAL §7.4's denial `where` was unfillable, and a number from one tier read
  as the other is wrong in a way that looks right.

### Invariants
- Reads still return quotations; a query result is a quote()-able node.
- The parser is TCB — prefer a proven artifact; a mis-parse must fail **closed** (reject), never admit.
- Rewrites go through `admit` + rebuild-on-write (D4), never edit a running fragment in place.

**Recommended entry: D5b-1** (declarative-profile checker) — it is small, unlocks sound config-proposal
review (the config-language pattern), and needs no full parser. Treat D5b-2/3/4 as a separately-gated
milestone, justified only by a concrete positional/source-rewrite/third-party-hardening requirement.
