# POM — the Program Object Model (v3)

*Companion to `FOUNDATION.md` §3. The POM is to the program what js_com's COM is to the
nginx configuration: a reflective tree, 1:1 with its source, reached only through
capabilities, with mutations classified by safety class. This document specifies the
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

**Identity rule (fragment identity / supply chain):** `id` is path-stable, `hash` is
content-stable. Policies **target** by id/selector but **pin** by hash — a policy pinned
to `F@hash₁` refuses to govern a silently edited `F@hash₂`; re-admission is required.

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

1. **Granularity floor** — materialize only module/function/block as first-class C-side
   nodes; synthesize statement/expression views on demand from spans (lean answer;
   confirm against selector needs).
2. **Selector-language grammar** — needs its spec note; same language as policy
   targeting, also the LSP/query surface.
3. **Cross-file provenance** — an `include`-spliced fragment gets a synthetic file
   origin; span mapping through includes needs a provenance hop.
4. **p_symbol enumeration** — the stable, versioned naming of node kinds across engine
   upgrades (carried from v2 §10.2; unchanged in importance).
