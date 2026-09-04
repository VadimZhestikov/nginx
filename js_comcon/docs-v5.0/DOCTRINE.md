# COMCON — Security Doctrine: confine, don't chase

> The *why* beneath the threat model. `THREATS.md` enumerates adversaries × assets ×
> mitigations; this document states the doctrine those mitigations serve — why COMCON fights
> vulnerabilities by **confinement** rather than **detection**, and where that leaves the
> effort that remains. Written to be read once, early, by anyone extending the system.

## 1. The result that dooms detection

Ask a scanner "is this program secure / vulnerability-free?" and you have asked it to decide a
**non-trivial semantic property of arbitrary code**. **Rice's theorem** says that is
**undecidable** — no algorithm decides it for all programs (the halting problem is the concrete
undecidable seed it generalizes; Gödel's incompleteness is the provability-side cousin, sharing the
same self-reference/diagonalization core).

The practical consequence is the treadmill everyone knows: you detect and patch a class of
vulnerability, and the space of *"unsafe in a way your current checks miss"* is still infinite. The
chase is endless **not because we are not clever enough, but because the property is undecidable.**
An adversary can, in principle, diagonalize against any fixed checker — construct exactly the input
that defeats it.

So a purely **detective** security posture (scan the untrusted code, look for badness) is provably
incomplete, forever. COMCON does not adopt it.

## 2. The doctrine

> **Do not fight vulnerabilities across the infinite space of untrusted code. Confine that space to
> irrelevance, then concentrate all correctness effort on the finite, analyzable TCB.**

The move that makes this work is a change of question, from **semantic** to **structural**:

- *Undecidable (Rice):* "Will this code do harm?"
- *Decidable (structural):* "What authority does it **hold**, and what can it even **express**?"

A fragment that never received a filesystem capability **cannot** touch the filesystem — whatever it
computes, however cleverly. That is not a behavioral claim to be proven; it is a fact about the
fragment's *environment*, checkable at admission. Confinement converts the undecidable behavioral
question into a decidable authority question — and *that* is why it escapes the treadmill.

Two escapes follow, and COMCON uses both:

1. **Confine authority** — capabilities. A fragment affects only what it holds.
2. **Restrict expressiveness** — profiles. Less that untrusted code can express ⇒ residual analysis
   becomes decidable/sound (a straight-line, loop-free declarative sentence *is* analyzable; full
   Turing-complete JS is not).

## 3. Pillar A — confine the untrusted space (structural, not analytic)

Make a tenant-code vulnerability *not matter* by construction:

- **Deny-by-default + capabilities.** `env()` is zero authority; every power is an explicit `grant`.
- **The admit gate is a sound rejecter.** `free-names ⊆ imports`: a fragment cannot even *reference*
  an ungranted name. Deliberately **sound, not complete** — may reject some safe code, never admits
  an unsafe reference. (Rice forbids a decider that is both; security chooses soundness.)
- **Realm isolation + only-data-crosses.** The fragment runs in a separate runtime; only JSON
  marshals across, never a live `JSValue`. A tenant cannot forge a reference into the host. This is
  the class where real bugs lived and were closed (getter-marshal reach-gate bypass, CRLF/framing
  smuggling).
- **Least-authority realization (R6) + propose-don't-hold.** When realizing an untrusted proposal,
  even the *operator's* authority is trimmed to the reviewed manifest (confused-deputy fix); and
  untrusted code returns *descriptions*, never holds live authority — the trusted host applies them.
- **Restrict expressiveness.** No dynamic code (M-SES-0); intrinsic freezing (M-SES-1,
  prototype-pollution isolation); declarative sub-Turing profiles for config. Shrinking what tenant
  code can express makes what remains checkable.

## 4. Pillar B — harden the finite TCB (the *only* place a real vulnerability lives)

Confinement moves all the trust onto a small, bounded surface: the kernel operators, the C plumbing
(compartment isolation, marshaling, reach gate), and the engine (QuickJS + the maxim JIT). Because
that surface is **finite**, this is where the heavy correctness effort goes:

- **Compiler faithfulness (SR-2).** A miscompile of untrusted code *is* a confinement escape, so the
  compiled tier must be conformant. This is the standing test262 JIT-conformance gate — not scanning
  tenant code, but proving a TCB component faithful so no tenant can exploit a divergence.
- **Adversarial pentest (SR-3).** Attack the confinement; fix what leaks (this found and closed the
  sibling iterator-prototype pollution). You test the TCB by trying to escape it.
- **Minimize + audit the surface.** Every COM operation exposed to untrusted code is a mediated,
  class-guarded capability; the reach gate and the `compartment_leave`-after-marshal discipline;
  fuzz the marshaling boundary. **The TCB must not creep** — each new mediated cap is trusted surface,
  so classify and gate it or don't add it.
- **Fail closed.** Deny-by-default, class-X guards, sound rejecters — the default is *refuse*.

## 5. Pillar C — meter what you cannot decide

Availability/termination is Rice again: you cannot decide whether untrusted code halts. So do not
try — **bound it.** Per-request execution `gas`/`meter` cuts execution at a budget on both tiers.
"Will it loop forever?" (undecidable) becomes "stop at N" (enforced).

## 6. Pillar D — name the residuals honestly

Confinement is not total, and claiming otherwise would be dishonest. `THREATS.md` accepts three
residuals by name — the things Pillar A cannot close by construction:

1. **Engine memory safety.** A memory-safety bug in QuickJS/maxim is a confinement escape we cannot
   *structurally* prevent — it is the **top residual**. Mitigation: keep the engine surface minimal,
   fuzz it, track upstream CVEs via the vendored-engine discipline, accept-and-document.
2. **IFC / side channels.** Capabilities bound *authority*, not *inference*; timing/cache leaks live
   below the model.
3. **Availability within reach.** A fragment may still burn its *granted* budget.

These get defense-in-depth and explicit documentation, not a false claim of closure.

## 7. Why this is effective (the finite-effort win)

The point is not a rearrangement — it is that confinement makes vuln-fighting **finite**:

| Detection posture | COMCON confinement posture |
|---|---|
| Decide "is arbitrary code safe?" — **undecidable (Rice)** | Decide "what does it hold / express?" — **structural, decidable** |
| Effort spans the **infinite** space of tenant behaviors | Effort spans the **finite** TCB |
| Incomplete forever; endless patch treadmill | Tenant-code vulns made *irrelevant*; TCB *verified* |
| Adversary diagonalizes against your checker | Adversary holds no capability to diagonalize *with* |

We do not ask "is this tenant safe?" We make the tenant *unable to matter*, and then we verify the
small thing we must trust. **We don't chase; we deny — and then we verify the finite TCB.**

## 8. Where to keep pushing (doctrine → priorities)

1. **Compiler faithfulness** — the test262 / SR-2 gate (the JIT is untrusted-native code generation;
   its conformance is a security property, not a quality nicety).
2. **Engine memory safety** — the top residual; fuzzing + CVE tracking on the vendored fork.
3. **A minimal TCB** — as COM-op vocabulary grows (the config-language pattern), every mediated cap
   is trusted surface; classify and gate it, resist creep.
4. **Soundness over completeness, everywhere** — new checks are sound rejecters that fail closed.
