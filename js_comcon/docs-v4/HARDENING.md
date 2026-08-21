# M-SES — Engine Hardening Milestone (v4)

*Companion to `SEMANTICS.md`. The stakes are exact: `grant` is unguarded, so
**unforgeability is not hygiene — it is the entire enforcement mechanism of the
possession axiom**. If capabilities can be forged via prototype pollution or
constructor ladders, the algebra is decorative. Scoped against our actual embedding
(QuickJS in pilgrim; Workers/SharedWorkers/SAB; the js_com COM surface; maxim's hybrid
fallback), not generic SES.*

---

## 0. Targets (U1–U3 as testable properties)

- **U1** — no expressible term denotes a capability except via the fragment's
  environment or kernel-op results.
- **U2** — capabilities are opaque: no operation extracts the underlying
  resource/pointer.
- **U3** — kernel internals and other fragments' authority are unreachable via
  prototype pollution, constructor ladders, `Function`/`eval`, stack/introspection
  leaks, or shared engine state.

**Boundary, stated up front:** capability discipline is **not** a memory-safety
substitute. A heap overflow in the C engine (regexp, GC) breaks everything above it.
That is a separate track: tracked upstream QuickJS updates, ASAN/UBSAN CI (already
running), fuzzing — and, noted only, per-tenant process isolation as defense-in-depth.

---

## 1. Phases

**S1 — Freeze & strip** *(small; do first — it informs everything)*.
Lockdown pass at compartment init: deep-freeze all intrinsics
(`Object/Array/Function/….prototype`, well-known symbols, accessors); strip ambient
authority from tenant globals — no `std`/`os` (QuickJS-libc!), no mutable `globalThis`;
`Date.now`/`Math.random` become **grantable determinism capabilities** (which the
`admit` rule already wants denied during tests). Engine helper: an intrinsic-freeze API.

**S2 — Compartments & module map** *(medium)*.
Per-fragment global object = the bound environment ρ, sharing only the frozen
intrinsics. `JS_SetModuleLoaderFunc` is per-**runtime** — a routing shim makes module
resolution per-compartment, so `import` resolves only to granted modules ("no free
import" made real). Cross-compartment identity: frozen intrinsics shared; everything
else membrane-crossed.

**S3 — Dynamic-code taming** *(small-medium; must be exhaustive)*.
The classic hole: removing global `Function` is useless while
`(function(){}).constructor` still works. The **intrinsics** `%Function%`,
`%GeneratorFunction%`, `%AsyncFunction%`, `eval` are tamed at construction — route to
`admit()` or throw. This is an **engine patch**, affordable because QuickJS is vendored
in-tree (and maxim already forks it). The probe list of indirect routes is derived from
test262. This phase implements the v2 compile-portal enumeration (v2 §3.6/§10.2b) as
enforcement.

**S4 — Host/COM facet audit** *(medium-large — THE LONG POLE, and genuinely ours)*.
Every C-backed host object reachable from a granted capability must not widen authority
transitively. Known offenders today: the **F2 cross-reference getters**
(`sock.listener`, `listener.socket`, upward links) — granting a leaf COM node currently
hands back paths toward the root. Fix: COM wrappers become **reach-respecting facets**
(traversal gated by the handle's reach), and `SharedArrayBuffer`/`Worker`/socket
constructors are de-ambiented into capabilities (SAB is real shared memory **and** a
covert channel ⇒ grant-only). U2 lands here too: no wrapper property may leak internal
pointers. This is v2 §9.4's "js_com API-factoring audit" made concrete.

> **Fuse S4 with milestone M2:** the facet audit and the typed-schema enumeration are
> the **same walk over the `describe()` registry** — each COM operation gets its type
> signature (M2) and its reach/facet rule (S4) in one pass.

**S5 — Resource guards** *(moderate)*.
Per-fragment CPU via `JS_SetInterruptHandler` + enter/exit attribution (gas);
`JS_SetMemoryLimit` per runtime. **Honest deferral:** QuickJS limits are per-runtime
and the heap is shared within a worker — fine-grained per-fragment *memory* attribution
is out of scope for v1 (coarse runtime limits + gas first). This implements v2's
"budget machinery is design-mandatory" (§3.3 residue 4, §3.5 invariant 4).

**S6 — Adversarial verification** *(the gate itself; initial suite medium, then
ongoing)*.
An escape-probe suite in `t/`: prototype-pollution probes, constructor ladders,
`.stack`/introspection leaks, `Symbol.species` attacks, cross-compartment identity
leaks, COM upward-traversal probes; plus ASAN/UBSAN runs, a fuzz corpus, and a signed
audit checklist. The deny-suite discipline of v2 §9.6.1 applies: every probe asserts
the *specific* denial, never merely "it failed".

---

## 2. The gate

> **M-SES GATE:** no probe in the escape suite can
> (a) obtain any value not in its environment,
> (b) mutate a frozen intrinsic,
> (c) create code from strings without `admit`,
> (d) traverse a COM facet beyond its handle's reach,
> (e) escape gas/memory limits without termination.

Coverage: U1 ⇐ S1+S3 · U2 ⇐ S4 · U3 ⇐ S1+S2+S4.

---

## 3. Scheduling & cost

- The compiler work (M2–M5) does **not** wait on M-SES — spikes and front-end
  development run trusted code; static rejection needs no runtime hardening.
- M-SES **gates M6/M7-with-tenants** — the first moment untrusted code executes.
- **Required even in the pure-AOT world:** compiled tenants re-enter the interpreter
  via the phase-34 **bytecode fallback** (dirty nodes, hybrid path). There is no
  "we compiled it, so skip SES" shortcut.
- Patches live in the in-tree `quickjs/` (upstream rebases get costlier — accepted;
  maxim already forks).
- Recommended start: **S1 immediately (cheap), S4 fused with M2.**
- Size: comparable to the compiler front-end itself; S4 is the long pole.
