# COMCON — Roadmap & Measured Results (v5.0)

> **WHERE WE ARE (v5.127 — 2026-09-16), in five lines.** Every increment is done and every
> track is closed: confinement (two signatures on the assurance case, ASSURANCE §15), the
> compiler (M5.1a and M5.1c shipped, M5.1b parked; the rule says NO-GO at 2.3× on the last
> class, PERFORMANCE §2f.3), the library (the vocabulary complete; `opaque.*` and the postures
> not built by decision), live ops and the config instance. The gate is `t/tools/gate.sh`,
> with a differential fuzz of the compiled tier as a stage; every negative control is
> automated; the OOM sweep stands on both tiers; F2's leak half is closed with its limit
> measured. **Open, by name:** F11's reproduction half (a signer running the pack); the
> broadcast-fuzz flake (its third face captured with the log tail in the v5.127 pack: every
> child reaped, the master idle 93 s, then a KILL aimed at a child it had reaped — see the
> FOUNDATION v5.127 delta); the engine's own `--jit-compile-all` test failures (maxim's, pre-existing, recorded in
> G7.23); and four decisions — the warm-speculation finding to post upstream, the sub-fragment
> narrowing words, `opaque.*` and the postures, M5.1b. The rest of this block is the log,
> newest first, each entry also a FOUNDATION delta.
>
> - **THREE LIBRARY-KIND GAPS CLOSED (2026-09-16, v5.127).** `std.evaluate` (the whole appetite
>   in one static read, G-13), `std.policy.diff` + `comcon.denials`/`ops.wouldDeny` (the
>   policy diff with an auto-safe verdict, and the gates attributed to the binding they fired
>   in — G-05), `std.docs`/`ops.docs` (the manual as a query over the contract, G-16). One
>   small mechanism (per-fragment denial attribution, with its control), the rest library
>   programs over shipped operators; `std.describe()`'s stale absent list corrected. Four
>   tests, three demos (O3, A2, A3): nineteen demos, 201 checks.
> - **THE SHOWCASES ANNOTATED WITH REAL CODE; THE GAPS REGISTERED; DEMOS FOR TWO NEW AUDIENCES
>   (2026-09-16, v5.126).** Every scenario heading carries a `REAL CODE` block (the shipped
>   spelling, the pinning tests, a gap id); `SHOWCASE-gaps.md` scores 53 scenarios (17 as
>   written, 4 respelled, 16 partial, 16 not built) and names 26 gaps by kind and home. Four
>   demos for live operations (epochs, fan-out, pins) and auditors (the trust report, the
>   query, the rewrite): sixteen demos, 168 checks.
> - **M5.1c BUILT AND MEASURED; THE M5 TRACK CLOSES; F20 AND F21 CLOSED (2026-09-16, v5.125).**
>   `charCodeAt` and `Math.imul` inlined by identity of the callee, doubles through ToInt32 in
>   place: class B 52.5 → 34.38 ns/char on the C bound 15.00, 2.3×, NO-GO by the rule. Building
>   it found F20 (five kinds of typed-lowering site reading the wrong slot, one linear inference
>   missing a value over a jump) and F21 (a compiled `~1.5` aborting the worker), both maxim's, both
>   fixed; `t/tools/jit-diff-fuzz.py` found F20's rest and is a gate stage (1,800 functions
>   clean). Flake hunt: three runs, no recurrence.
>
> - **THE RECORD MADE READABLE; CLASS B RE-MEASURED (2026-09-16, v5.124).** README's 83 KB
>   history line is a five-line state; ROADMAP opens with where we are. Class B's typed arm is
>   a C engine-read kernel: 15.16 vs 52.5 ns/char, 3.5× — GO by the rule, where v5.114's NO-GO
>   stood on a lowered-JS denominator. The sweep covers `checkRequest`. The broadcast flake's
>   first face carried text (`kill(worker, 9) failed`: the master's TERMINATE escalation at
>   shutdown, under the pack only); the harness now prints the log tail on a "no alerts"
>   failure.
>
> **POSITION (v5.123 — 2026-09-15).** Increments **A / B / C are done** (COMCON-lite
> core; typed admission front-end; compiled tier C5–C7 with the SR-2 faithfulness gate passed).
> Increment **E (M-CFG / config instance)** is **substantially built**: the kernel-operator
> surface (`comcon.{env,grant,mediate,bind,admit,include,mode}`) shipped and the
> **CONVERGENCE (INCREMENT_CONVERGE.md) is complete** — one confined mechanism
> (`comcon.include` + `location.handler`) on both tiers, with the `js_tenant_*` directives
> removed.
>
> Increment **D (live POM ops)** is **COMPLETE (2026-09-12).**
> `INCREMENT_D.md` is authoritative: D0 (substrate) · D1 (lazy NodeView) · D2 (`query(sel)`) ·
> D3 (quotations + stone splices) · D4a (epochs/replace/rollback) · D4b (class-F multi-worker
> fan-out) · D5a (call-site audit) · D5b-1 (declarative-profile checker) ·
> **D5b-2 (full CST + finer selectors + anchors)** · **D5b-3 (source-rewrite hardening)** ·
> **D5b-4 (cross-file provenance)** · **D4c (compiled tier under a live epoch switch, all
> 2026-09-12)** all ✅ — **nothing in increment D is left open.** Fifteen files cover it:
> eleven `t/comcon_pom_*`, plus `t/comcon_declarative*`, `t/comcon_parser_vendor.t` and
> `t/comcon_aot_epoch.t`. D5b-2 also closed §4 item 1 (anchor recognition), the last
> unbuilt item of the minimal first slice.
> **SHOWCASE §38 (harden code you will never touch) is now built end to end** — audit
> (D5a) + kernel enforcement + source rewrite for the residual the kernel cannot name.
>
> **The maxim-finalization gate is CLEARED (2026-09-11).** The test262 JIT sweep that used to
> abort ~54% in on an atom-table assertion now completes: 14/14 shards, 49,402 files, **55
> failing files — all 55 in the known-errors baseline, 0 new, 0 crashes**. So "full test262"
> is no longer what stands between here and untrusted-native.
>
> **M-SES is closed out.** S1–S6 built, SR-1/SR-2/SR-3 passed, the S6 *standing* escape gate
> landed 2026-09-11 (`t/comcon_mses_gate.t`, ASAN+UBSAN via `t/run_sanitizers.sh`), and
> **`AUDIT_M-SES.md` §5 was SIGNED 2026-09-12** after a full §4 re-run on a rebuilt tree.
> Read the caveats in that file before relying on the signature: **both rows carry the same
> signer**, so it is one attestation rather than two with no second pair of eyes, and the
> security row **accepts §3's five gaps as residual risk rather than closing them**
> (per-fragment memory attribution · cross-compartment identity · `guarded`/`irreversible`
> COM members · compiled tier under the escape probes · host JS unbounded by default).
>
> **THE CONFINEMENT TRACK IS CLOSED. THE LIBRARY IS STARTED AND ITS REMAINDER IS BLOCKED.
> THE COMPILER TRACK IS PARKED, AND NOW HAS THE EVIDENCE TO STAY THAT WAY.**
>
> **"CLOSED" SURVIVED THREE REAL ESCAPES FOUND AFTER IT WAS SAID (F15, 2026-09-14),
> BECAUSE EACH WAS FOUND AND FIXED BEFORE ANYTHING SHIPPED ON IT.** Investigating one
> named, minor residual (an unmetered top-level eval) turned up two defects that were
> WORSE than what was named: a fragment could reassign a shared global for every OTHER
> fragment, and a fragment's own source text could escape the wrapper it is compiled
> inside, defeating admission ENTIRELY — even under `imports: []`, the strictest
> contract an operator can write. All three are closed (v5.102–v5.104, below); the
> track's claim stands, but "closed" describes today's tree, not a proof that no
> fourth defect is waiting the same way: all three were found by INVESTIGATING, not
> by a scheduled review, and the pattern across this session is that composing
> shipped features finds what auditing them in isolation did not.
>
> - **M5.1a SHIPPED (2026-09-15, v5.117): class A's lowered scan 11.72 → 1.26 ns/byte, within
>   2.1× of the typed bound, nothing assumed.** Two codegen changes (`JIT_CODEGEN_VERSION`
>   18): a bit op with one provably-numeric operand yields a typed int32 — the spec gives the
>   accumulator its type, so no loop versioning and no declared shapes were needed — and an
>   in-bounds integer typed-array element is read in place, bounds-checked against the count
>   the engine keeps current. Rejected as unsound: versioning (out of bounds is `undefined`)
>   and feedback speculation (a miss substitutes 0). Four SR-2 cases with the spec's values
>   written out, validated by breaking both paths; G7.18's ten probes unchanged. Class B
>   unmoved (its loop is a call and a `>>>`), and its "typed bound" was lowered JS — corrected
>   in PERFORMANCE §2f. By the M5.0 rule the remaining 2.1× (boxing and checks) is NOT worth a
>   further cut: **M5.1 is complete as measured.** ASSURANCE G7.20.
> - **THE GATE IN THE TREE; G7.22 MEASURED; THE STREAM SURFACE SWEPT; ARTIFACTS UNTRACKED
>   (2026-09-15, v5.123).** `t/tools/gate.sh` is what "gate green" means, with `--configure`
>   for the four builddirs; `objs_jit/` is no longer tracked. G7.22's named under-count
>   measured at 100% kept in three provoked configurations (`t/comcon_retained_backstop.t`),
>   the correction unchanged. The residue sweep's twelfth shape: a stream server's handler
>   receiving an uncaught out-of-memory, one alignment per connection. The pack's
>   INCONCLUSIVE rows did their job: two patches re-based after F2 moved their lines, one
>   false SKIP (an assertion's prose) fixed. The warm-speculation finding could not be filed
>   upstream — issues are disabled on the maxim repository — and its text is with the author.
> - **F2's LEAK HALF CLOSED (2026-09-15, v5.122).** What a fragment RETAINS across calls is
>   charged to it — the compartment's malloc delta around each invocation, exact for what
>   refcounting frees, corrected for cycles by a per-call collection when a call leaves
>   64 KB or more behind and a 4 MB backstop, O(1) for ordinary work (the heap-independence
>   instrument reads 0.98×) — capped by `meter({retainedBytes})` (8 MB default, narrowing
>   only) and REFUSED past the cap with `E_MEM_RETAINED`, not run, until the epoch is
>   replaced; a sub-fragment charges its own slot under the cap in force and is refused
>   inside its parent; `comcon.memStatus(f)` reads the count. The user's four decisions:
>   refusal not denial; 8 MB; own slot; a host reader. 19 assertions on both binaries, the
>   golden corpus carries the code. What the accounting cannot attribute is named in
>   ASSURANCE G7.22 (a leaker beside a sibling making many small cycles is under-counted at
>   the backstop) and falls to the runtime cap. AUDIT_M-SES §3's first residual is closed.
> - **EVIDENCE FROM A FAILED CONTROL; WARM SPECULATION OFF; THE SWEEP WIDENED (2026-09-15,
>   v5.121).** The controls script keeps `prove -v` output and the test's directory when a row
>   fails to hold, and the broadcast fuzz asserts a worker received a broadcast, so the flake
>   that spoiled two pack runs is a failing assertion with per-worker counts. Every warm
>   value speculation in the engine is off behind `JIT_WARM_VALUE_SPECULATION 0` — read site
>   by site, three read with no tag check on a miss, one substituted 0, one leaked; the two
>   write sites were sound; unreachable here, carried to the fork. The residue sweep has
>   eleven shapes (F18's at a 16-byte step, include inside a full parent, an admission
>   `tests` that fills memory, a rejected promise's reaction), 80 assertions, no worker died;
>   the stream surface stays unswept. F2's leak half has a DESIGN awaiting a decision
>   (per-fragment retained accounting, amortized GC correction, a `retainedBytes` word and an
>   `E_MEM_RETAINED` refusal) — a mechanism and a vocabulary word, so not built unasked.
> - **THE NEGATIVE-CONTROL DEBT PAID (2026-09-15, v5.120).** Twenty rows both signatures had
>   accepted as manual are maintained reverse patches under `t/tools/controls/` — each the
>   smallest change that brings its defect back — verified by the same script as the commit
>   rows: an engine patch rebuilds the library, a leak row runs under `objs_asan` and looks
>   for the named frame, a skipped test is INCONCLUSIVE, and a patch that stops applying fails
>   the run and is re-based on purpose. The reviewer pack prints INCONCLUSIVE rows instead of
>   a MANUAL list. Step 4 of the same plan: maxim's warm element hint that substituted 0 on a
>   type miss is off in the vendored engine (unreachable here; hygiene; carried to the fork).
> - **THE RESIDUE SWEEP AS A BATTERY; F19 (2026-09-15, v5.119).** F18's class lives in a byte
>   window a sanitizer MOVES, so the S6 corpus cannot see it; `t/comcon_oom_sweep.t` sweeps
>   seven places the allowance can bite (catch, finally, generator, a job after await, a
>   sub-fragment with the parent catching, the host's marshal, the catch handler's own
>   allocation), 32 alignments each, both tiers, every fragment authored at config phase so the
>   compiled arm lowers it: no worker died anywhere (54 assertions). Its first run found
>   **F19**: the host's ToString of a fragment's error ran out of memory itself, reported
>   `error` for an out-of-memory it could name and left its exception pending — fixed in
>   `ngx_js_comcon_exc_text`. ASSURANCE G7.21.
> - **M5.1b PARKED WITH ITS NUMBERS (2026-09-15, v5.118) — THE M5 TRACK CLOSES AT M5.1a.**
>   (B) declared shapes at the boundary: the remaining gap is 2.1×, below the 3× rule, and it
>   would add a second source of truth for a value's type. (A) a host typed view of request
>   bytes: the host passes the request as JSON, so it needs a new crossing and an opt-in word;
>   it pays 12.5 ns/byte over a `charCodeAt` scan — 2.5 µs for a uri, noise against the 1.1 µs
>   boundary; 200 µs for a 16 KB body — and the record holds no policy that scans bodies in JS.
>   A product question. If it appears: one capability word, copy-backed, never a zero-copy
>   view (E2 measured no gain; a retained view would alias freed memory). PERFORMANCE §2f.1.
> - **A SECOND SIGNATURE (2026-09-15, v5.116).** The reviewer pack run in full on `4a86d2a62`
>   (twice — run 1 tripped the broadcast-fuzz flake on one automated control, run 2 passed
>   every gate; transcripts under `reviews/`); a second signer, Dick Hardman, accepted the
>   evidence and the residuals (`REVIEW.md` §4, ASSURANCE §15, AUDIT_M-SES §5). The pack was
>   executed by the authoring session, so F11's reproduction half stays open — the row's
>   "I ran it myself" clause is struck, not softened.
> - **F18 FOUND BY M5.0's GATE AND CLOSED (2026-09-15, v5.115).** An out-of-memory inside
>   the engine's own backtrace annotation freed the pending exception under its own feet:
>   `build_backtrace(ctx, rt->current_exception, …)` held no reference, so a fragment that
>   exhausts its allowance at the right residue killed the worker (`/nestmemory`, 3/3 on this
>   layout, never under a sanitizer — a sanitizer moves where the allowance bites); on the
>   same path an uncatchable abort would have lost its flag. Closed in the engine
>   (`build_backtrace_pending`: hold a reference; restore the error and its flag if the
>   attempt threw), with a sweep that hits the window on any layout
>   (`t/comcon_oom_backtrace.t`), validated against the unfixed engine. ASSURANCE G7.19.
>   The patch is carried to the engine fork.
> - **M5.0 DECIDED (2026-09-15, v5.114): class A GO at 19.2×, class B NO-GO at 2.5×.**
>   `t/tools/m5-go-nogo.t`, rule stated first (`lowered / typed ≥ 3`), `typed` = the
>   achievable typed-shape bound (gas-checked C walk for bytes; per-char engine access for
>   strings). Byte-scan validation: typed 0.61 ns/byte vs lowered 11.72 → **GO**. Token
>   check: typed 20.16 ns/char vs lowered 50.00 → **NO-GO**, re-parked with its number.
>   The gas check costs nothing; untyped lowering buys the data plane only 1.4×. **M5.1 is
>   therefore narrow: typed-array element access and int32 accumulators** — the byte load
>   and the int op with the gas check kept — every lowered case under SR-2
>   (`t/comcon_include_faithfulness.t`), every erasable gate under G7.18
>   (`t/comcon_compiled_resource_gates.t`). PERFORMANCE §2e. **Next: M5.1's first cut —
>   the scope decision (which typed shapes, how types are proved: M4's admission IR vs
>   maxim's own inference) before any codegen.**
> - **M5 STEP 3 (2026-09-15, v5.113): M8's harness is SR-2's differential, now holding M5.0's
>   two fragment classes** (byte-scan validation; a string-heavy token check) — both lower,
>   both agree — **and a hole in it is closed**: the first version's cases failed identically
>   on both tiers at include (newlines in a single-quoted literal) and 27/28 assertions passed,
>   because equal is true of two identical failures; each case now asserts what a correct
>   response looks like. Step 1's clean pack on `469fa5161`: ALL GATES PASS, transcript
>   delivered. **Next: M5.0, the go/no-go benchmark** — the two classes interpreted vs
>   untyped lowering vs the hand-written typed-shape arm (`t/tools/lowering-ceiling.t`).
> - **M5 IS UNPARKED (2026-09-15, v5.112) — EVIDENCE FIRST.** The user unparked the compiler
>   track. The order: (1) a second signer for SR-4 (the reviewer pack, run in full — green;
>   one run spoiled by a file the authoring session wrote into `t/` mid-run, re-run clean for
>   the signer); (2) **the compiled tier's resource gates as a standing battery**
>   (`t/comcon_compiled_resource_gates.t`, 36, two arms from one file): deadline through
>   catch, catch-and-spin, finally, a compiled generator, async, and a compiled sub-fragment
>   inside a compiled parent's catch; budget, redaction and allowance in compiled loops —
>   every probe agrees with the interpreter, nothing widened; **async fragments are not
>   lowered at include time** (compiled == 0 on the JIT arm), which M5.0 must know; (3) M8's
>   "T2 refines T1" as a differential harness over typed fragments; (4) M5.0, a go/no-go
>   benchmark on two real fragment classes against the typed-shape arm; (5) M5.1, the codegen,
>   only if M5.0 says yes. ASSURANCE G7.18.
> - **`subFragments` IS A LIVE COUNT (2026-09-15, v5.111 — D8, decided by the user).** The
>   sub-fragment callable is an object with a `call` handler and a finalizer: drop it and the
>   slot is released, the count refunded, immediately. A parent authoring per request spends
>   nothing lasting. The remaining open items: `uses`/`window`/`cosign`/`protocol` not writable
>   by a sub-fragment; facets not narrowable from inside; depth two; the design-level backlog.
> - **F17 FOUND AND CLOSED — THE SANITIZER CORPUS NOW DETECTS LEAKS (2026-09-15, v5.110).**
>   Step 2 below, run once with `detect_leaks=1`, reported 4 KB the gate had never seen: a
>   WORKER never freed the compartment at exit (`exit_process` tore down the tenant and host
>   runtimes, not `comcon_rt`). Fixed with one `ngx_js_comcon_teardown()` for all three exit
>   paths — and freeing it where fragments ran makes `JS_FreeRuntime`'s assertion a leak check
>   of every invocation path, which held over 84 files. With the corpus then run leaks-on, one
>   file still reported: the COM node classes (`NginxServer`, the per-module nodes, upstreams)
>   had IDs and prototypes in the compartment runtime but NO class definition, so
>   `listener.serverByName()` inside a fragment under audit minted wrappers freed without their
>   finalizer — opaque + 4 KB pool per call, unboundedly (T11). Registration moved into
>   `ngx_js_com_register_classes()` for every runtime. The corpus ran leaks-off because of one
>   by-construction allocation (the SW manager thread's parked `pollfd`); that is now a one-line
>   suppression (`t/tools/lsan.supp`) and the corpus runs `detect_leaks=1`. ASSURANCE G7.17.
> - **THE REMAINING ITEMS, ONE AT A TIME (2026-09-15, v5.109): two done.** (1) The engine debt
>   is paid: the four engine files are carried to the pilgrim-quickjs fork's `pilgrim` branch
>   at byte parity under its own self-gate (local commit `8984e3f`; not pushed; `run-test262.c`
>   flows the other way on the next pull). (2) The author capability's two gates are probed in
>   both postures (`t/comcon_author_audit.t`, 15): `cap.owner` reached through a leftover
>   drained as nobody, and UNCONDITIONAL — denied in audit too, as v5.96 decided for every
>   wrapper; `cap.expired` ordinary — allowed in audit, logged once per gated operation.
>   **Still open:** `uses`/`window`/`cosign`/`protocol` not writable by a sub-fragment;
>   facets not narrowable from inside; depth two; sub-fragment slot release; and the older
>   backlog (the broadcast-fuzz flake, the `serverName` config-phase gap, `std.postures.
>   lockdown`, `opaque.*`, F2's leak half, F11's second signer, M5 parked).
> - **THE AUTHORING TIER IS COMPLETE — PHASE 4, THE CLOSE-OUT (2026-09-15, v5.108).** Nothing
>   new ships. SEMANTICS §3 carries the (AUTHOR) induction step and names F16's class under
>   assumption (F) — the lowered C must simulate the RESOURCE gates too, which the S6 battery
>   (what a fragment can reach) cannot see. PERFORMANCE §2d: one nested invocation ≈ 0.59 µs on
>   top of the outer 0.72 µs — less, as its shape predicts (no enter/leave, posture, settle or
>   drain). SPEC §8a is normative (eight contract words, three attenuation words, the refusals);
>   OPERATOR_API §8j, MANUAL §3.8, THREATS T13, SHOWCASE17 §8 (rewritten around what was built,
>   with its two honest limits), INCREMENT_MLIB §4. Phase 1's `min()` is finally pinned where
>   nesting exists (a sub asking 3 s inside a 500 ms parent dies at 500 ms; one asking 16 MB
>   inside 2 MB runs out at ~109 × 16 KB), and a sub-fragment's job is pinned running in the
>   parent's drain. `verify-negative-controls.sh`: one automated row (phase 3, verified) and
>   five MANUAL rows with instructions. **Open, listed:** `uses`/`window`/`cosign`/`protocol`
>   not writable by a sub-fragment; facets not narrowable from inside; depth two; the author
>   gate under audit mode; sub-fragment slot release.
> - **THE AUTHORING TIER, PHASE 3 — RE-GRANTING, BY COPY (2026-09-15, v5.107).** A parent
>   hands its OWN wrappers down (`grants: {name: cap}`) narrowed by data words
>   (`attenuate: {name: {allow|redact, ttlSeconds}}`); each child is a COPY of the parent's
>   opaque with the owner changed, and only the mask (AND, subset asserted for `allow`) and
>   the expiry (min) move, downward. Never a re-wrap of the handle — phase 0 found that would
>   mint a FRESH wrapper from a stale parent; measured instead: after the host closes the
>   socket, the parent, its cached sub-fragment and a re-grant made from the stale parent
>   all answer the same. Both arms agree with the host-side meet (with a control that
>   differs); a session-typed wrapper is not re-grantable (its cursor is one conversation).
>   Two of my readings were corrected by the evidence (`redact` only removes; no mask word
>   means unchanged). `t/comcon_author_regrant.t` (34). ASSURANCE G7.16. **Phase 4 remains:
>   SEMANTICS §3's induction step, PERFORMANCE's depth-2 row, negative-control rows,
>   SHOWCASE17 §8, INCREMENT_MLIB §4.**
> - **THE AUTHORING TIER, PHASE 2 — A FRAGMENT CAN AUTHOR FRAGMENTS (2026-09-15, v5.106) —
>   AND F16, FOUND BY IT.** `comcon.author({subFragments: N})` is granted like any capability;
>   `author.include(source, {imports, ...})` runs the SAME admission pipeline the host runs
>   (`comcon.include()` split into four stages on compartment values; two entrances, no second
>   copy to drift — check [2] now lists the two stage functions as the only compile portals)
>   and returns a callable sub-fragment: own handle and identity, own deadline and allowance
>   nested inside the parent's, the parent's posture. Text crosses the nested boundary, never
>   objects (a closure returned or thrown to the parent would run under the parent's
>   identity); nested invocation is synchronous and drains nothing (the job FIFO is shared);
>   a sub-fragment past its deadline aborts the whole invocation, one past its allowance
>   raises an ordinary exception as at the host boundary; `E_AUTHOR_LIMIT` names the budget
>   and the depth cap. **The stop condition held:** the S6 battery answers at depth 2 exactly
>   as at depth 1, probe by probe (`t/comcon_author_depth2_gate.t`; the battery is one
>   definition composed two ways, because `globalThis` is a denied name no admitted fragment
>   can carry). **F16:** the parent CAUGHT its sub-fragment's abort on the JIT build only —
>   a maxim-compiled fragment could catch its own deadline interrupt, because the generated
>   catch dispatch never checked the engine's uncatchable flag; the hostile form would loop
>   forever. Fixed in the engine (`JS_IsUncatchableException()`, one guard, codegen version
>   17), pinned on both builds by `t/comcon_jit_uncatchable.t`. The F5 battery could not see
>   it: it asks what a fragment can REACH, and this was what a fragment can REFUSE TO STOP
>   DOING. ASSURANCE G7.14, G7.15, F16. **Phase 3 (re-granting) and phase 4 (docs, controls)
>   remain.**
> - **THE AUTHORING TIER IS STARTED — PHASE 1, NESTING READINESS (2026-09-15, v5.105).**
>   The v5.98 bullet below measured that "authoring does not nest at all"; the decision taken
>   is to BUILD it — a granted `author` capability whose one operation is `include(src,
>   spec)`, returning a callable sub-fragment that is a real fragment (own handle, own
>   owner, own deadline) and can hold only what its parent holds, narrowed. Five phases:
>   0 (verifications — done; two rules in the plan turned out unnecessary, one got more
>   precise), 1 (this delta: the invocation's memory allowance and deadline behave as a
>   STACK — push what you found, restore what you found — and a depth counter keeps the
>   settle loop and both drains at depth 1, because the job FIFO is shared), 2 (the
>   capability, nested include and a sync-only nested invoke, no re-grants; the depth-2
>   escape battery over `t/tools/mses-probes.js` is the stop condition), 3 (re-granting by
>   COPY-THEN-NARROW of the parent's wrapper, `mask`/`ttl` only in v1 — never by re-wrapping
>   the handle, which reads `gen` from the global table and would launder a stale parent),
>   4 (docs, assurance, enumerations, negative controls). Nothing nests in this tree yet;
>   v5.105 changes nothing observable and pins the shape. FOUNDATION v5.105 · ASSURANCE
>   G7.13 (addendum).
> - **F15 CLOSED, ALL THREE PARTS (2026-09-14, v5.104).** The last part, and the ORIGINAL
>   finding: comcon_rt's interrupt handler required a worker before it was installed at
>   all, so a fragment's own top-level evaluation, a confined invocation, and an
>   admission test that calls its fragment were ALL genuinely unbounded at CONFIG PHASE
>   (`js_source`, including `nginx -t`) — measured, each **hanging until killed**. Fixed
>   by giving comcon_rt a deadline of its own (`jcf->comcon_deadline_ms`), independent of
>   whether a worker exists, installed once at compartment creation with no post-fork
>   re-wiring needed. **A push around the wrong call was found and corrected the same
>   session, by testing the claim rather than trusting it**: the first attempt bounded
>   the call that only MATERIALIZES the wrapper's closure, not the later one that
>   actually RUNS its body — where the looping source that named this finding loops.
>   `t/comcon_deadline_without_worker.t` (9). ASSURANCE G7.13.
> - **F15 PHASE 2 (2026-09-14, v5.103): a fragment's own text cannot escape the wrapper
>   it is compiled inside.** `comcon.include()` compiles a fragment by concatenating it
>   into `(function(g0,...){"use strict";return(SRC)})` and compiling the WHOLE buffer;
>   admission only ever inspected the RESULT, never the rest of the script. A source that
>   closed the wrapper early ran its escaped text with **no admission gate at all**, even
>   under `imports: []`. Fixed: compile with `JS_EVAL_FLAG_COMPILE_ONLY`, then verify the
>   compiled unit's bytecode is exactly "create one closure, return it" — checked against
>   the compiler's own opcode output, not a second parser — before ever running it. **A
>   first version of the check (count nested closures) was found incomplete before
>   shipping**: sufficient for the fragment wrapper, not for `contract.tests`'s bare-paren
>   wrapper, where a side effect needs no second closure at all.
>   `t/comcon_wrapper_breakout.t` (13, two controls). ASSURANCE G7.12.
> - **F15 PHASE 1 (2026-09-14, v5.102): a fragment cannot reassign a shared global for
>   every other fragment.** An ADMITTED fragment body (`imports: ['Promise']`) could do
>   `Promise = evil` and corrupt every co-resident fragment's view of it; an UN-ADMITTED
>   fragment (`{}`, no admission at all) could do the same to any intrinsic with zero
>   gating. **A claim signed 2026-09-12 (G7.6) was found INCOMPLETE, not false**: its
>   eight-surface cross-identity battery tested only VALUE mutation, never BINDING
>   reassignment — the ninth operation, and the one that was open. Fixed by freezing
>   every binding on the compartment's globalThis once, at creation, independent of
>   admission. `t/comcon_global_binding_freeze.t` (8). ASSURANCE G7.11, G7.6 corrected in
>   place.
> - **F14 CLOSED (2026-09-14, v5.101): a confined invocation no longer costs what the
>   rest of the compartment holds.** Every invocation walked the WHOLE shared compartment
>   heap (`JS_ComputeMemoryUsage`, twice per call) to read one counter — measured, one
>   worker, one trivial confined call: **22.0% of stock throughput idle, 0.2% (476 req/s)
>   while another fragment held 200,000 objects**, a cross-tenant channel F8's own
>   measurement could not see (memory a peer merely HOLDS, beyond the execution
>   deadline's reach). Fixed via an O(1) engine counter (`JS_GetMallocSize`): now 68.0% /
>   66.6%. Also fixed in the same investigation: out-of-memory reported as `null`
>   (identical to a fragment doing `throw null`), and `include()` errors that reached the
>   host with no value at all. `t/comcon_invoke_heap_independence.t` (ratio-gated:
>   1.01×, control 173.57×) · `t/comcon_fragment_error_report.t`. ASSURANCE G7.10, G6.19.
> - **G6.16's ACCOUNTING HALF CLOSED (2026-09-14, v5.100): a fragment's leftovers are
>   charged to nobody.** The job queue is FIFO, so a fragment's leftover continuations
>   ran FIRST inside the next invocation — measured, 12,000 leftovers queued ahead of it
>   consumed a fragment's ENTIRE 10,000-job budget, so its own continuations never ran at
>   all. And a leftover's AUTHORITY depended on who arrived next (`cap.owner` compares
>   against the fragment now running) — allowed when its own fragment happened to be
>   invoked again, denied otherwise. Fixed: leftovers drain at the START of an
>   invocation, as nobody, under a budget and deadline of their own.
>   `t/comcon_leftover_accounting.t` (9). ASSURANCE G6.18.
> - **THE L4 FILTER WINDOW WAS NOT A WAIT STATE (2026-09-14, v5.99) — the ~2.8% p17
>   flake, closed, and two worse defects found alongside it.** Between accept and
>   `ngx_http_init_connection()` a connection with an L4 filter armed had **no deadline
>   of any kind**: a client sending zero bytes held a connection slot and its pool until
>   reload, and the worker aborted at graceful shutdown over a connection stock nginx
>   would have held with a non-cancelable timer. Fixed by arming the same timer nginx's
>   own wait state arms. Found alongside it: **13 sites** closing a pre-http connection
>   with `ngx_close_connection()` instead of `ngx_http_close_connection()`, leaking the
>   connection pool — measured, 199 rejects leaked 101,888 bytes.
>   `t/js_pilgrim_p17_l4_window.t` (8) · `t/js_pilgrim_p17_reject_leak.t` (7).
> - **M-LIB IS NEARLY COMPLETE.** Steps 1–2 shipped 2026-09-12 (`comcon.std.profiles`,
>   `std.ops`); **the mediation vocabulary is ten of ten** and the posture fields are read
>   (v5.67 → v5.91). *(This bullet said the opposite for three days — that the posture
>   vocabulary and `allowHosts`/`ttl`/`window` still "need C-side enforcement" — after all
>   four had shipped. Nothing caught it, which is why the line below is now enforced.)*
>   What is left needs a DECISION or a new mechanism, not enforcement:
>
>   **NOT BUILT (canonical list — enforced by check [8]):** `opaque.*` · `std.postures.*` ·
>   raw operators withheld
> - **M-CFG's config instance SHIPPED 2026-09-12 (v5.60)** — *one tenant subtree onboarded
>   through admit end-to-end*, the last named deliverable of increment E, and the first thing
>   to compose D5b-1's sound rejecter, D3's quotations, M4's typed registry and `std.ops`.
>   `comcon.std.config` (`review`/`diff`/`apply`/`rollback`), `t/comcon_config_instance.t`.
>   Composing them found what the parts could not: `apply()` was not atomic.
> - **[TBD-2] IS RESOLVED (2026-09-12, v5.62)** — the denial/explain schema's code
>   half, which M2.5 asks to be designed *with* the descriptors rather than after.
>   Two axes: DENIAL codes (run-time gates) and thirteen REFUSAL codes (admission),
>   closed in the C enum, carried on the thrown Error as `.code`, enumerated by
>   `comcon.refusalCodes()`, frozen by V12 and checked by enumeration checks [5]+[6].
>   Still open by name: `E_BUDGET_*` and the JS capability layer's `E_CAP_FLAVOR` /
>   `E_CAP_ESCALATE` — the next tranche. It found a real defect: `contract.tests` was
>   silently ignored unless it was a string, so `tests: [fn]` was admitted with the
>   behavioural gate never run.
> - **M-LIB STEP 3 SHIPPED (2026-09-12, v5.67): `uses` — the first enforced budget.**
>   The mediation vocabulary's first attenuation of RATE rather than reach, fleet-wide in
>   `nginx.shared` (measured: 4 workers, a limit of 10 spent exactly 10 times), +0.037 µs
>   per charged use. It also settles where `E_BUDGET_*` belongs: exhaustion is a DENIAL
>   (`budget.uses`), not an admission refusal, so that family stays empty by design.
> - **F3 PROBED (2026-09-12, v5.66) — and its residual later WITHDRAWN, see v5.78 above:** cross-compartment identity, the audit's
>   NOT-EVIDENCED row. Host↔fragment turns out to be STRUCTURAL (separate
>   `JS_NewRuntime()`s — a by-reference control kills the worker instead of leaking),
>   while fragment↔fragment shares one runtime and one context, so the M-SES-1 freeze is
>   the mechanism: **removing it opens five of seven probed surfaces**. (The `Symbol` residual
>   recorded here was **WITHDRAWN 2026-09-13** — see v5.78 above: the probe that found it
>   compared a value with itself.)
> - **TM-2 IS CLOSED (2026-09-12, v5.65):** the identity→environment mapping, the last
>   unowned finding in THREATS.md and the one that had to exist *before the first real
>   operator session*. FOUNDATION §8b owns it; `comcon.std.sessions` implements it as
>   **descriptors, not environments** — so the table carries no authority, is fleet-wide,
>   and can only NARROW the env of whoever resolves a principal. The residual is stated
>   rather than hidden: **COMCON does not authenticate**; the host asserts the principal.
> - **A NOT-BUILT LIST IS NOW ENFORCED, AND "CAGES NEST FOR FREE" IS MEASURED (2026-09-14,
>   v5.98).** Enumeration **check [8]**: a canonical NOT-BUILT line may not name a word the code
>   ships — the rot that let this very block call four shipped words unbuilt for three days. It
>   parses one machine-readable line per document rather than prose, because the first version
>   flagged a DATED bullet that was true when written. And SHOWCASE17 §8's reseller scenario is
>   half unbuilt: **attenuation nests without limit, authoring does not nest at all** — `comcon`
>   reads `undefined` in a fragment and granting it is `E_CAP_GRANT`, which the S6 gate already
>   asserted as an escape. `t/comcon_nesting.t` (8).
> - **THE LOWERING CEILING IS MEASURED (2026-09-13, v5.97) — AND IT REFRAMES M5.** `nginx.bench` +
>   `t/tools/lowering-ceiling.t`, numbers in PERFORMANCE.md §2b. Untyped lowering is **8.3× off
>   hand-written C on arithmetic, 17× on a byte scan**, and `--jit-dump-c` shows the cause: every
>   op boxes a `JSValue` and tag-checks both operands, with the accumulator in a `double`. **A
>   typed-SHAPE arm, gas check kept, is at parity** — so parity is reachable and the whole prize is
>   whether inference can drop the boxing. Corrected three of our own beliefs: the zero-copy
>   `ArrayBuffer` view **buys nothing**, a host call per byte ≈ a compiled typed-array read, and
>   this file's own 13× control is **loop elimination**. **M5 stays parked** — but the commitment
>   is now a measurement, not a judgement.
> - **THE AUDIT-MODE RESIDUAL, CLOSED (2026-09-13, v5.96):** `cap.owner` denies in every mode. The
>   line is not "structural vs policy" (the reach gates are structural too) but **whether an operator
>   has anything to observe and then enable** — every other code asks "may this fragment do this?"
>   (about the GRANT, their lever), this one asks "is this even this fragment's capability?", which
>   no grant can change. Logged as `mode=audit … unconditional=1`. The test asserts the DISTINCTION —
>   a closed window allowed and a foreign capability denied in the same audit request. 3 controls.
> - **A GRANTED CAPABILITY BELONGS TO ONE FRAGMENT (2026-09-13, v5.95)** — the structural half
>   v5.93 named and owed. Its drain is best-effort, so a fragment outrunning the job budget leaves
>   work behind; **the question changes from stopping the code to stopping its AUTHORITY.** Every
>   granted wrapper (socket, outbound, COM facet) records its fragment; every gate asks first;
>   `cap.owner` is the first denial code naming a structural invariant rather than a policy. The
>   probe FORCES the residual (10,100 deferred requests) instead of arguing about it, and the facet
>   gets the same treatment so its check is not code no control can break. **The boundary was in
>   the wrong place and a test from another increment caught it:** SR-1 materializes the result
>   INSIDE the compartment, so a getter on the returned object is fragment code — identity, posture
>   and allowance now all end where the compartment does. `t/comcon_cap_owner.t` (12) + 6 controls.
> - **V10 BUILT (2026-09-13, v5.94) — AND IT FOUND A DEFECT.** The last V-item independent of the
>   parked compiler track. The mode fan-out's epoch bump was three JS operations, so two concurrent
>   switches both wrote epoch N+1 with different modes — and the reconciler's early return on epoch
>   EQUALITY made the loser's divergence **permanent and silent** (a fleet on `enforce` leaving one
>   worker in `audit` for life). The publish is now one critical section under the store's lock; the
>   reconciler adopts on GREATER, which is what makes the rollout monotone. **The model said the
>   one-line fix was insufficient before the code was written.** F9 drops from four unmodelled
>   V-items to three (V5a, V5b, V6 — all compiler-dependent). `t/tools/check-epoch-model.py` +
>   `t/comcon_v10_epoch_model.t` (7) + 3 controls.
> - **THE DEFERRED-JOB ESCAPE, CLOSED (2026-09-13, v5.93) — opened by v5.92 one day earlier.** A
>   fragment could queue a job and return; nothing else drains the compartment runtime, so it ran
>   inside the NEXT unrelated invocation — on a stranger's deadline and memory allowance, gated at a
>   stranger's wall-clock time, and under a stranger's `onViolation` POSTURE. Every invocation now
>   drains to quiescence. **Two of the fix's first attempts were wrong and their controls said so**
>   (the "job threw" signal cannot fire — it is an unhandled rejection, and nothing had installed a
>   tracker on either runtime; and the posture assertion could not discriminate until the fleet and
>   the binding disagreed). **A v5.92 claim was wrong:** the drain's real bound is the MEMORY
>   ALLOWANCE, not the clock. The S6 battery gains an ASYNC arm. `t/comcon_deferred_jobs.t` (14) +
>   3 controls.
> - **ASYNC FRAGMENTS SHIPPED (2026-09-13, v5.92) — AND THE BLOCKER WAS NOT WHERE THIS ROADMAP
>   SAID.** It recorded the synchronous invoke. An async fragment never reached the invoke: it was
>   refused at ADMISSION as "not a bytecode function", which is untrue of an async function — six
>   COMCON analysis entry points tested one class id where the engine has a four-class helper, so
>   the C3 analysis **refused to look** at async and generator bodies. Fixed at all six; the promise
>   is settled by draining the compartment's OWN jobs, bounded by the deadline AND a job cap (which
>   changes the MESSAGE: pending, not timed out), and an unsettleable promise is reported as
>   `E_INVOKE_PENDING` rather than stringified into `{}`. **Still not `fetch`** — the blocker has
>   moved from "the invoke is synchronous" to "the host cannot yet suspend the request across a
>   fragment call", which is a smaller and better-specified problem. `t/comcon_async_fragment.t`
>   (16) + 4 controls.
> - **THE POSTURE WORDS SHIPPED (2026-09-13, v5.91): a posture belongs to the BINDING.**
>   `onViolation` and `profile` were in MANUAL since v5.0 and read by nothing. The material find is
>   granularity: the audit/enforce switch was FLEET-WIDE, so **shadowing one tenant's new policy
>   also stopped enforcing every other tenant's.** One request can now hold a shadowed binding
>   beside an enforced one, in either direction relative to the fleet; restored in C including on
>   the exception path. `profile` is read by being REFUSED where it cannot be honoured. Remaining
>   from MANUAL: `std.postures.*` (a decision nobody has made, not a missing mechanism) and
>   `opaque.*`. `t/comcon_posture.t` (10) + 3 controls.
> - **M-LIB `protocol` SHIPPED (2026-09-13, v5.90) — THE MEDIATION VOCABULARY IS COMPLETE, ten of
>   ten.** Enforced operation ORDER (`cap.protocol`); `protocol('fd')` is a ONE-SHOT capability,
>   which `uses(1)` cannot express. **It enforces order, not completion** (a fragment can simply
>   return). **Its gate is the first to separate its DECISION from its EFFECT** — checked before
>   cosign, committed after the budget. **And its test found a defect in `cosign`:** an
>   already-consenting principal was judged by POSITION in the record, not the record's LENGTH, so
>   the first signer's RETRY was denied forever — breaking the real ops-room sequence. Remaining
>   from MANUAL's list: `opaque.*` (never a mediation — an engine-substrate question) and the
>   posture words. `t/comcon_cap_protocol.t` (19) + 4 controls.
> - **THREE DEFECTS FROM ASKING v5.87's QUESTION ONE AXIS OVER (2026-09-13, v5.89).** Probing each
>   word ALONE found a `window` gap; probing each word alone **on each capability KIND** found that
>   a bare `uses`/`ttl`/`cosign` over an outbound cap was refused as "not a NginxSocket" (**the word
>   carries no type**), that the outbound budget key was **not namespaced** so one `uses` name was
>   two counters, and that `JS_ToCStringLen` on a missing property returns the STRING `"undefined"`.
>   All fail-closed; the budget one falsified a documented property. `t/comcon_bare_mediation.t`
>   (7) + 4 controls.
> - **M-LIB `cosign` SHIPPED (2026-09-13, v5.88): the TWO-PERSON RULE.** The first word that
>   bounds WHO rather than when or how often, and the only mediation a holder cannot satisfy
>   alone (`cap.cosign`). **The hard part is not the counter, it is who is counting:** COMCON
>   does not authenticate, so `as` is written on the trusted side and is unreachable from inside a
>   compartment — one identity per invocation, one vote, so **distinctness is structural**. The
>   quorum therefore assembles ACROSS INVOCATIONS and there is no `approve()` verb: **the attempt
>   is the consent**, so a `cap.cosign` denial is the only one in the set that is a waiting state
>   and the only one with a SIDE EFFECT. Fleet-wide record (the SET of principals, not a count);
>   a third lattice shape (`quorum` MAX, `within` MIN); `E_CAP_PRINCIPAL` is the 14th refusal
>   code. **NINE OF TEN VOCABULARY WORDS SHIP.** A control caught a wrong instrument again — the
>   expiry probe read `req.args.as`, which is a raw query STRING, so both requests voted as the
>   same principal. Remaining: `protocol` (enforced operation ORDER), `opaque.*` (engine
>   substrate), the posture words.
> - **M-LIB `window` SHIPPED (2026-09-13, v5.87) — and its probe found a defect in the INVOKE.**
>   A recurring lifetime beside `ttl`'s countdown (`cap.window`, UTC by decision, wraps midnight,
>   whole-day, two schedules refused). **Eight of ten vocabulary words ship.** Probing the word
>   ALONE found a gap composition hid (a bare grant was refused), and the probe's natural shape
>   found that a fragment returning `undefined` — **what every denied gate returns** — produced
>   `SyntaxError: unexpected token: 'undefined'`.
> - **THE OUTBOUND ROUND TRIP (2026-09-13, v5.86).** `std.outbound.perform()` + a test where a
>   policy asks, the host performs against a real backend, and the policy **decides from the
>   responses**. `allowHosts` globs may pin the SCHEME (matched exactly) — which is NOT MANUAL's
>   `protocol` (enforced operation ORDER, still unbuilt; I had assumed otherwise and checked).
>   **Two of four controls did not fire**: one documented property was FALSE (`clear()` took no
>   count) and one unmeasured. Both fixed and asserted.
> - **M-LIB `allowHosts` SHIPPED (2026-09-13, v5.85): the OUTBOUND capability.** The last
>   word blocked on a missing mechanism. **It is not a `fetch`, and that is a finding:**
>   fragment invocation is synchronous, so the cap RECORDS INTENT and the host performs the
>   I/O — M-CFG's "the tenant proposes what it cannot apply". Gates: `out.host` (destination)
>   and `out.drain` (the host's half, reach-gated). Composes with `uses`/`ttl`; two globs are
>   refused. **Seven of ten vocabulary words ship.** Remaining: `window`, `cosign`, `protocol`,
>   `opaque.*`, the posture words — and a real `fetch` waits on ASYNC fragment invocation.
> - **THE REVIEWER PACK (2026-09-13, v5.84): F11 is no longer expensive.** `reviewer-pack.sh`
>   + `REVIEW.md` turn "read 1100 lines, extract the commands, know which builddirs are stale"
>   into one command and a verdict table. **F11 stays OPEN — it needs a person** — but a
>   reproduction now costs an afternoon. Refuses on a dirty tree, rebuilds every builddir and
>   asserts binary freshness, separates GATE from REPORTED, and deliberately does NOT summarise
>   the residuals a signer accepts.
> - **FLAKE HUNT (2026-09-13, v5.83): the gate's one unexplained failure was MINE.** 25 full
>   runs, 24 pass, 1 fail — `js_com_propagation.t` test 13 looked the sweeping worker up in a
>   fan-out that need not have reached it (absent in 4/40 under load). Fixed by reporting its
>   own count. **My original stability check was 3 runs, and 60 STANDALONE runs of the broken
>   code also pass** — the condition needs full-suite load. The older unexplained failure is
>   still unattributed, now with a denominator: not reproduced in 25 runs.
> - **V9 BUILT (2026-09-13, v5.82): describe ⊇ mutable now covers the PROGRAM instance —
>   seven undescribed ops.** Four surfaces each carry a hand-written op list next to the
>   members it describes. `describe` itself was classified on both NodeViews and absent on
>   both epoch handles. **Auditing the fourth surface found the last two.** **F9 is down to
>   four, ALL on the parked compiler track: the reachable verification backlog is EMPTY.**
> - **V14 BUILT (2026-09-13, v5.81) — and its claim was FALSE.** The same fragment compiled
>   to a different `.so` every run, differing in six bytes: GCC records the translation
>   unit's filename and it came from `mkstemps`. **Signed bytes therefore attested which
>   compile produced an artifact, not what is in it.** Fixed with one content-derived
>   basename. **Half the first fix was inert and only the control said so.** F9 down to five,
>   with **V9 the only reachable one left**.
> - **F13 CLOSED (2026-09-13, v5.80): the request surface is classified — 54 rows, every one
>   request-scoped.** The tenant-facing object had no type and no class while every
>   config-phase node had both. Table rows (per-class), not the bare-name read-only map, so
>   `requestScoped` is a fact rather than a guess — and a getter added without a row now
>   FAILS. Every type read off its getter; none wrong on the first run. **This was the last
>   OPEN finding in the ledger.** Remaining: F8 and F11 (both ACCEPTED) and F9's six V-items.
> - **THE TESTS ARE GATED (2026-09-13, v5.79): 39 assertions claimed something and checked
>   nothing.** V11 mutation-tests the policies; nothing tested the tests, and five dead
>   assertions surfaced in this arc BY ACCIDENT. `check-dead-probes.py` + `comcon_dead_probes.t`
>   gate four shapes. 27 padding deleted, 8 duplicates removed, **4 genuinely untested
>   behaviours now checked — and all four claims were TRUE, which is why they survived.**
>   Static only: clean means "not dead in the four known ways".
> - **F3 CLOSED (2026-09-13, v5.78): its `Symbol.for` residual is WITHDRAWN as a dead-probe
>   artefact.** The read compared `Symbol.for(k) === Symbol.for(k)` inside ONE fragment. The
>   rewritten probe attempts the real exploit and is refused on every surface: a shared
>   registry is a shared NAME, and a name is no channel without a STORE. **A per-fragment
>   `Symbol` facet was built and rejected — it breaks erasure (G11.7).** Two controls: the
>   unconfined arm reads the mark back, and the freeze-disabled arm opens the channel.
> - **V8 COMPLETED (2026-09-13, v5.77): the effect-class half — `propagation` is TRUE.**
>   The one registry column nothing had ever checked, and uncheckable in one process. Its
>   only conditional claim (the peer refine hook) is verified on both sides in one fixture,
>   **with the run asserting the two arms DISAGREE**, plus a sweep generated per row: 67
>   `worker-local` members stamped in one worker, none leaked. A leaf that says a column is
>   true rather than reporting a bug. Coverage stated (67 of 154; 52 booleans carry no
>   distinguishable sentinel; `auto-shared` has zero rows). **Next here:** M2's request
>   surface (F13) — the tenant-facing members with no type and no class.
> - **M2.5 RE-STATED (2026-09-13, v5.76): the spec states current truth, and check [7]
>   keeps it there.** The normative read had fallen behind on five shipped features. The
>   durable half is the checker, not the edit — and it matches BACKTICKED identifiers,
>   because a bare-word search for `log` or `mode` would pass on any prose while reporting
>   green. The refusal codes stay delegated to `comcon.refusalCodes()`: a spec that copies a
>   table acquires a second place for it to be wrong.
> - **V8 BUILT (2026-09-13, v5.75): the registry's READ-ONLY half is now held to
>   account per row** — `t/js_com_schema_conformance.t`, generated from the live walk.
>   Found one real misdeclaration (`names`), **20 reached rows with no declared type**, and
>   that **`nginx.describe(req)` returns ZERO rows**: the tenant-facing request surface is
>   outside the registry (F13). That makes the read-only descriptor's hardcoded
>   `requestScoped:false` unfalsifiable rather than wrong — and the test pins the request
>   row count at zero so adding those rows must fix the field in the same change. **F9 is
>   down to five unbuilt V-items.** Next here was V8's effect-class half — **done at v5.77,
>   above** — then M2's request-surface classification, which is what S4 needs for reach.
> - **M-LIB STEP 4 SHIPPED (2026-09-13, v5.74): `ttl`, a capability lifetime.** The
>   composition hole between TM-2's session lease and `include()`'s admission-time
>   binding: the mapping expired while the authority did not. Lifetimes compose by `min`
>   (they are ordered) where budgets are refused (they are not) — same rule, different
>   lattice. Six of the ten vocabulary words now ship.
> - **F4 CLOSED, F9 REDUCED, F8 MEASURED (2026-09-13, v5.73).** The guarded COM members
>   are fuzzed **one process at a time** (the shared-state objection required isolation,
>   not an exemption); **V13 is built** (erasure checked against node — a different
>   engine, which is the point), leaving six unbuilt V-items; and **T9 now has a number**:
>   a peer's latency moves 0.3 ms → 347 ms (1227× idle, ~2.9 bits/s), narrowed to 49.8 ms
>   by a 50 ms execution deadline — accepted, but no longer unquantified.
> - **F12 CLOSED + F2 PARTLY CLOSED (2026-09-13, v5.72).** The host deadline is armed at
>   every entry that runs request JS (`w->current_request`, 8 sites, one helper — an
>   `await` no longer resets the bound), and a confined fragment gets a per-invocation
>   memory allowance (16 MB default, contract may only narrow). F2's remainder — a slow
>   leak across calls, which the shared runtime cap still backstops — stays open.
> - **F6 CLOSED (2026-09-13, v5.71): host JS is bounded by default.**
>   `nginx.workerRequestTimeout` now defaults to 10 s (0 = explicit opt-out, malformed =
>   the default). The old default of 0 meant one accidental `while(true)` hung a worker
>   until SIGKILL. **Closing it exposed and named F12:** the deadline bounds one
>   SYNCHRONOUS entry, so a continuation after an `await` is still unbounded — the
>   time-gap heuristic that would cover it was rejected (under load the worker never
>   idles, so a cumulative clock would abort every request).
> - **F10 CLOSED / [TBD-2] FULLY RESOLVED (2026-09-12, v5.70):** `E_CAP_FLAVOR` and
>   `E_CAP_ESCALATE` ship — the latter one code for one rule over four raising sites, two
>   of which are defence-in-depth and recorded as unprobeable rather than given a dead
>   probe. The two EMPTY families are answers: `E_BUDGET_*` (exhaustion is a denial) and
>   the deadline abort (the engine's interrupt). Fifteen refusal codes, six denial codes,
>   all frozen by V12.
> - **F5 CLOSED (2026-09-12, v5.69):** the M-SES escape battery now runs against a
>   fragment with **20 natively-lowered functions** (`t/comcon_mses_gate_aot.t`), with the
>   precondition asserted — compiled arm `compiled>=1`, interpreted arm `0` — and the two
>   tiers agreeing probe by probe. Recorded in ASSURANCE §16 as a change after the
>   signature. The audit's last PARTIAL row is now closed.
> - **SR-4 IS SIGNED (2026-09-12, v5.68) — the last standing gate is closed.**
>   `ASSURANCE.md` §15: signer **Vadim Zhestikov**, tree at `d6ed62395`, on a full re-run
>   (four builddirs rebuilt; both suites on both builds; ASAN+UBSAN clean in `src/js` with
>   the positive control firing; six enumerations; the case checker; the instruments).
>   **One signer, commands run by the authoring session, and the eleven findings are
>   ACCEPTED as residual risk rather than closed** — so what is missing is a second pair of
>   eyes, not an artifact. The re-run itself found two things worth the exercise: the M5
>   instrument measures only on `objs_jit` (its guards fail the run rather than reporting a
>   false 1.0×), and automated falsifiability fell 8/8 → 6/8 because this session's own
>   commits rewrote the lines two inverse patches target. **RE-BASED 2026-09-13:** those two
>   rows are now MANUAL with their reason, so the battery reports 6/6 with no inconclusive
>   bucket — the debt is labelled, not paid (six rows need a hand revert). `git apply -R -3`
>   is recorded as a trap: it half-applied and left the partial revert in the tree.
> - **SR-4 was BUILT (2026-09-12): `ASSURANCE.md`, the V15 assurance case.** The claim →
>   assumption → evidence tree, machine-checked by `t/comcon_assurance.t` so it cannot rot,
>   with a findings ledger of eleven. **It is NOT SIGNED**, and signing it is the act that
>   actually closes the gate. Its first finding (F1) was that **20 of 83 evidence citations
>   in this doc set pointed at files deleted by the convergence** — a quarter of what a
>   reviewer would try to follow.
> - **The verification track has run ahead of its column.** V3/V4/V7 (now/M2–M3) and
>   **V11 + V12** (nominally M7/M8) are all built as of 2026-09-12 — see `VERIFICATION.md`.
>   V12's finding was acted on the same day (the [TBD-2] entry above), so §3.2's advice to
>   tenants — "pin to codes, not message text" — is now followable on both axes. Still open
>   on the V-track: V5a/V5b,
>   V6, V8, V9, V10, V13, V14, V15.
>
> - **Confinement (increments A–E + D): no open item.** One thing inside D4c was
>   deliberately NOT built and should not be mistaken for an oversight: **re-AOT of a live
>   epoch inside a worker**, which the fork model forbids (the gcc thread does not survive
>   `fork()`). A rewritten epoch runs the bytecode fallback — correct and coherent, and
>   `comcon.aotStatus()` reports it. Reaching native would need a compiler-bearing process
>   to build the `.so` and workers to pick it up from the hash-keyed JIT cache: new IPC, a
>   separate increment, no correctness impact.
> - **HOST-PERF ✅ DONE 2026-09-12 (v5.63).** Both host-path costs the M5 evidence turned
>   up are fixed and re-measured A/B on one box: `shared.incr`'s linear scan is an
>   open-addressed hash table (**miss 0.420 → 0.084 µs, 5.0×**; the 12.7× cliff over a
>   typed stub is now 2.6×) and `req.headers` is materialized once per request
>   (**0.130 → 0.043 µs**). **The M5 precondition is discharged:** the gap a typed ABI
>   could still close is ~0.03 µs per call, against 0.38 µs that was the scan. See the
>   HOST-PERF entry in §2 for the table and the one named residual (`incr` keeps its
>   counter as a string and re-parses it per call).
> - **The compiler track (M5 →) remains parked by decision 2026-09-11, not by capability.**
>   M5's value is the typed nginx stubs, not lowering JS control flow, so the typed IR is not
>   to be built without a commitment to M5 (see M4 below and `AOT-A` in `INCREMENT_C5.md`
>   for the measurement that settled it: 5.79× on compute-bearing JS, **1.0× on a
>   host-call-dominated policy**).
>
>   **EVIDENCE FOR THE COMMITMENT QUESTION, measured 2026-09-12**
>   (`t/tools/policy-compute-split.t`, in-process A/B — a throughput benchmark on this box
>   runs through WSL2's mirrored-mode firewall, which compresses every ratio toward 1.0 and
>   would manufacture the answer). Four policies people actually ask for, interpreted vs
>   `jitCompile()`d, three runs:
>   **count_tag 0.93–1.07× · ratelimit 1.02–1.28× · jwtish 0.91–0.96× · routing 0.78–0.93×**,
>   against a known-positive control at **20.6–24.5×**. So the harness can see a speed-up and
>   these policies simply do not have one. The `jwtish` case matters most: a realistic
>   200-character token, a split and a hash loop — real work, still 0.92×, because the time is
>   in the RUNTIME's string machinery rather than arithmetic the compiler can unbox.
>   **"Compute-bearing" means arithmetic in JS; string-heavy policies do not benefit either.**
>
>   **The minimum stub set is single digits.** Extracted from the same policies with the
>   D5b-2 CST: `nginx.shared`, `req.headers`, `req.uri` — **3 members**, against a classified
>   COM surface of **339 members across 55 rows**. So a stub ABI for policies of this shape is
>   not "type the COM surface"; it is roughly `shared.incr`, a header lookup and a uri read.
>   That is the number the commitment turns on, and it makes M5 a milestone rather than a
>   track — *provided* the payoff is taken from the stubs, which is where these measurements
>   say it lives.
>
>   **One documented precondition is already met.** R4 warns that generated C is not covered
>   by `JS_SetInterruptHandler`, so "until back-edge gas lands, the tier-2-eligible profile is
>   loop-free". Measured: a natively-lowered fragment (`aotStatus` `compiled:1`) running an
>   8-billion-iteration loop under a 150 ms meter is **interrupted at 150 ms**, exactly like
>   the interpreted one.
>
>   **AND THE PAYOFF IS NOT WHERE THE THESIS PUTS IT** (`t/tools/host-call-cost.t`,
>   2 M iterations, identical on both tiers). Decomposing the host call these policies make:
>
>   | per call | µs | |
>   |---|---|---|
>   | a plain JS function call | 0.034 | for scale |
>   | JS→C dispatch, boxed args | 0.039 | **the ABI floor — about the price of a JS call** |
>   | + `JS_ToCString` of the key | 0.033 | **not measurable; string marshalling is free** |
>   | typed stub, slot resolved | 0.034 | what M1's slab atomic looked like |
>   | + the spinlock | 0.041 | |
>   | **`shared.incr` today** (near-front hit) | **0.079** | +0.045 is the LINEAR SCAN |
>   | **`shared.incr` on a miss** (217 keys) | **0.417** | +0.38 is the scan |
>   | `req.headers['x-tenant']` | 0.130 | |
>   | …with `req.headers` hoisted once | 0.030 | **+0.10 is materializing the surface** |
>
>   So the costs a typed stub ABI **uniquely** removes — boxing, marshalling, dispatch — total
>   about **0.035 µs, roughly one JS call**. The costs that dominate are a **linear scan over
>   256 slots with 128-byte key compares** (2.3× on a hit, 12× on a miss) and **re-materializing
>   `req.headers` on every access** (0.10 µs). **Both are host-side and fixable today, with no
>   compiler**: an index instead of a scan, and a per-request cached headers surface.
>
>   M1's hand-written C did not have either cost — it used a slab atomic on a resolved slot —
>   which means a substantial part of its **3.44×** is a data-structure result being attributed
>   to compilation. **Before committing to M5, fix those two and re-measure the gap**; what
>   remains after that is the honest size of the compiler's prize.
> - **What is open and unblocked is M-LIB** — and it became unblocked quietly, when M3
>   completed: M-LIB is specified as "authored *in* the policy language once M3 exists".
>   There are **20 kernel operators and no `std.*` at all**, while `MANUAL.md` is written
>   as-if-shipped against `std.profiles.tenant(acme)`, `std.postures.lockdown` and
>   `std.ops`. The kernel is finished and unusable by anyone who does not already know it;
>   that gap, not another kernel feature, is the next piece of value. **STARTED
>   2026-09-12** (see M-LIB below).
> - Also due by §12's own table and never done: the **now/M2–M3 verification column**
>   (V3 executable reference semantics · V4 monotonicity-as-assertion · V7 generated
>   enumerations).
>
> One standing instruction survives the signature: **adding findings re-stales the date the
> audit certifies**, so a new hardening round should be a deliberate choice to re-sign, not a
> drive-by.
>
> Entries below that describe `js_tenant_*` / `comcon_load` / a separate tenant path as the plan
> are superseded by the operator kernel; see FOUNDATION §12 (v5.29–v5.35) for the delta.

*Merges v2 §8 (staging) and §11 (first slice) with the typed-policy→maxim milestone
plan and the M1 gate results. Ordering is de-risked: validate the payoff before
building the compiler; two hard gates (M1 performance, M8 safety) plus the hardening
gate (M-SES) before any untrusted execution.*

---

## 1. Positioning

- **COMCON is step 1.** Policies double as **compilation contracts**: a fragment whose
  environment (imports), exports, and internals are fully described is closed enough
  for AOT.
- **maxim** (QuickJS fork; JS→C via bytecode→C→GCC/TCC, typed vars, phase-35
  server-AOT, phase-34 hybrid `.so` = C fn + bytecode fallback) is **step 2**,
  consuming policy-annotated typed fragments.
- **mirror as a policy:** the mirror transpiler is a compile-time transformation — an
  *adaptive-profile* policy in v3 terms.

The pipeline: typed COMCON policy (.js, restricted+typed) → parse restricted subset →
capability check (compile-time reject) → type-bind against the typed host-API schema →
maxim lowering to unboxed C against typed nginx stubs → per-tenant `.so` (hybrid
fallback) → the event dispatcher calls the C function pointer directly.

---

## 2. Milestones

- **Config languages — a USERLAND pattern, not a milestone (reframed 2026-09-03).** Briefly scoped
  as "M-DSL / increment E," then retired on review: a config-DSL is fully expressible with shipped
  primitives (the reduction principle), so it is **up to operators/devs** — the platform builds
  nothing new. Operators mint a language by granting a vocabulary + `realize`ing sentences, and
  handle untrusted config via **propose-don't-hold** (the confined sentence returns a cap-free
  description; a trusted host apply loop validates + applies it). A `defineLanguage` operator would
  duplicate `env`+`grant`+`realize` (same reason `includeAt`/`serve` were retired). The single
  genuine platform hook — **sound declarative-profile review** (`syntax_allowed` + descriptor
  tables) — folds into **increment D5b** (the CST front-end). Pattern + recipes in
  **`PATTERN_config_language.md`**.

- **M1 — Perf spike (hand-written C, no compiler). ✅ GATE PASSED 2026-08-16.**
  Hand-wrote the C a compiled "count + tag" policy would become (shm-atomic counter +
  request-header read + two response headers + body) as a static nginx module;
  benchmarked against the *same policy* as an interpreted mirror rule on an identical
  config skeleton. Results (h2load, HTTP/1.1 keepalive, best of 3):

  | Config | loopback | % | cross-host 10GbE | % |
  |---|--:|--:|--:|--:|
  | stock nginx (no policy) | 391,233 | 100% | 424,882 | 100% |
  | **hand-C policy** | 350,165 | **90%** | 409,386 | **96%** |
  | interpreted mirror | 113,327 | 29% | 119,044 | 28% |

  Hand-C = **3.1–3.4×** the interpreted mirror. A *reduced-policy control* (the
  directive-expressible subset run additionally as pure nginx `map`+`add_header`
  config) showed directives ≈ hand-C ≈ stock — **the whole gap is the interpreter**,
  and the compiler's payoff is precisely the policies directives cannot express
  (shared state, counters, routing). Artifacts: `t_performance/maxim_m1/` (branch
  `js_comcon`). These are the measured endpoints of the performance gradient
  (FOUNDATION §8); v2's cost model (§8.2) is no longer only argued.

- **M-UNIFY — one engine tree (v5.1 — E5, user decision).** Merge maxim's ~54 phases
  into the vendored `quickjs/` tree (rather than moving pilgrim onto maxim's fork).
  Rationale: the fat-bytecode artifact couples the engine that runs T1 with the
  compiler that consumes it — **one tree = one bytecode definition, one hardening
  surface (M-SES patches land once), one upstream-tracking burden**. Prerequisite for
  the fragment artifact (§10); schedule before M5, ideally alongside M2.

- **M2 — Typed nginx-API schema.** Machine-readable static type signatures for the
  policy-visible host surface (mirror `ev.*` first: header/cookie reads, response-header
  writes, table get/set/incr, respond/selectUpstream), each op carrying param/return
  types, required capability, effect class, and C-stub signature. **Fuse with
  hardening S4** — the same walk over the `describe()` registry yields the type row
  and the facet/reach rule per op (HARDENING §S4). *(v4)* The schema is **dual-role**:
  it types the capabilities granted to policies *and* it is the type system of the
  admissible config surface (M-CFG) — design it with value-domain constraints, not
  only function signatures, so the second role is not precluded. *(v5.0 — V1
  companion rule:)* **no host op may expose a numeric domain exceeding the safe-integer
  range** — the numeric model (SEMANTICS §6) is sound in the language but would leak
  through the library without this: expose **ms** timestamps (never ns — ~1.7×10¹⁸
  exceeds 2⁵³), scaled units for large quantities, and strings/opaque handles for true
  64-bit identifiers. Enforced by the V8 conformance tests per registry row. *(v5.5 —
  from the reality check:)* the walk must **add rows for the read-only getters** — the
  current `describe` tables deliberately omit read-only members
  (`ngx_js_com_describe.c:41`), but those getters (`sock.listener`, `server.locations`,
  `listener.serverByName`, …) are exactly the reach/leak paths S4 must give a facet
  rule; the `type` column is already a stringly-typed signature slot, so M2's type
  signatures are a pure data extension.

- **M2.5 — THE SPEC (scope expanded, v5.4; RE-STATED 2026-09-13, v5.76).** `SPEC.md` was
  delivered and then fell behind the code — no `routes`/`ttl`/refusal codes/`cap.expired`/
  session registry, §10 still calling identity→environment a future deliverable, §13 stamped
  v5.35 against a delta log at v5.75. §2/§10/§13 now state current truth, **and check [7] of
  the enumeration checker makes the closed sets' currency machine-checked** so this cannot
  recur silently (four controls; presence, not correctness — see ASSURANCE G11.9).
  *Original scope:* originally "consolidate SEMANTICS + POM";
  now: produce **one clean normative SPEC of the entire v5.x design** — the
  implementer's read — with zero inline archaeology (the `(vN.M — Rx)` annotations move
  to a history appendix; this document set remains the design record). The rewrite
  doubles as the final consistency check (contradictions cannot hide behind version
  tags) and absorbs: the **term-discipline sweep** (fragment≡node vs instance;
  environment/signature≡manifest; retire `import_list`), the **layered-core framing**
  (COMCON-lite = the design minus {compiler, tiers, quotations, adaptive, POM-rewrite,
  config-instance} — the design is layered, not monolithic), and the note that the
  artifact may be content-addressed by `H(source ∥ schema-version)`, folding the pin
  and schema checks into one identity match. After M2.5, M3's capability check is
  simply "does this reference resolve in the bound environment."
  *Expanded per showcase lessons (§5):* also deliver (a) the **selector/target
  language grammar** (promoted from open questions — the showcases used it constantly
  and invented syntax ad hoc), and (b) the **denial/explain schema** (promoted — it is
  the operator UX, the deny-suite assertion language, the learning record, and the LSP
  diagnostic; design it with the descriptors, not after — *(v5.4 — TM-1:)* including
  **per-fragment denial-log quotas with sampling above quota** (exact per-code
  counters, sampled full records, quota-exceeded itself reported) so a tenant looping
  on a denied name cannot exhaust disk or drown the audit signal — *(v5.1 — E9, scope
  reduction:)* the selector deliverable is staged down: **v1 = five registered
  deterministic combinators as library functions** (`module(glob)`, `callsites(name)`,
  `exports(f)`, `anchors(n)`, `within`) implemented as NodeView walks — sufficient for
  R9's re-evaluable born-bound semantics; the general query *grammar* becomes a later
  ergonomic upgrade). *(v4)* Also state the
  kernel's **instance-genericity** (admit parameterized by grammar+schema; one pattern,
  N instances), the **∅-environment principle**, and the rights-not-values meet rule
  for data instances (FOUNDATION §2a, SEMANTICS v4 additions). *(v4.1)* Plus the
  **third closed enumeration**: the ops-resource capabilities (denial log,
  binding/epoch store, provenance registry, class-F broadcast channel, snapshot store,
  signing key, learning-recorder switch) — first-class caps so tool verbs are pure
  library code and "no backdoor" is checkable (FOUNDATION §8a). *(v4.2)* Explicitly
  **not** a deliverable: any standalone composed policy grammar — quotations quote
  policy-JS itself, "declarative" is a `syntax_allowed` profile, descriptor tables are
  the admission normal form (SEMANTICS §4.4); the selector grammar deliverable stands;
  the M3 front-end does double duty (tenant code *and* policy quotations).

- **M-CFG — the admissible config surface. ✅ DELIVERABLE SHIPPED 2026-09-12**
  (`comcon.std.config`, `t/comcon_config_instance.t`, `INCREMENT_MCFG.md` §"second phase"):
  one tenant subtree onboarded through admit end to end — propose, review (typed against the
  registry, pinned by hash), diff audit-first, refuse by SAFETY CLASS rather than by a
  blocklist, apply all-or-nothing with the guarded op named, roll back exactly. **The proposal
  never executes**: it is reduced to a descriptor table and the operator applies it.
  *(Original scope, preserved:)* Give COM its admission hinge: tenant config fragments = sentences of a
  restricted config grammar, admitted like code — `syntax_allowed` over config
  productions, typed against the dual-role M2 schema, contract tests, pin-by-hash,
  audit-first rollout. Includes quotation-based config proposals (tenant proposes what
  it cannot apply; the operator realizes — formalizing the snapshot/rollback console)
  and config learning-mode harvest. Deliverable: one tenant subtree onboarded through
  admit end-to-end. Gives the 3-layer operator-reconfig UX plan its principled
  foundation.

- **M-LIB — standard policy library. 🚧 IN PROGRESS — steps 1 and 2 shipped 2026-09-12**
  (`INCREMENT_MLIB.md`): `comcon.std` with `profiles.tenant(env,opts)` /
  `profiles.pure_library(opts)` / `describe()`, imports **derived** from the env so the
  manifest cannot drift from the grants, tenants **bounded by default**, and the mediation
  vocabulary **closed** — step 1 had to fix a fail-open first: an unimplemented flavor
  (`allowHosts`) or a typo (`redcat`) fell through include()'s translation to
  `mask:FULL` and granted the capability IN FULL, so a misspelling widened authority.
  `std.describe()` names, per contract field, what enforces it — and names what is absent,
  because the one rule for a profile is **only fields the kernel enforces**: MANUAL's
  `postures`/`onViolation` are NOT shipped, since nothing reads them and a posture of
  ignored keys would be believed. `t/comcon_std_lib.t` (19) + 7 controls.
  **Step 2 — `std.ops`, administration as library code** (FOUNDATION §8a, "there is no
  management plane"): a session **takes its ops-resource capabilities as arguments** and a
  verb whose resource was not passed is ABSENT, so the no-backdoor property is visible by
  `Object.keys()` — a session given nothing has only `describe()`. All seven §8a resources
  are enumerated **including the two with no host spelling** (`provenance`, `signing` →
  `host:null`), which is what makes those gaps checkable rather than invisible, and
  `describe()` walks the same table the session is built from (**V7 discharged for this
  enumeration**). Fifteen verbs; **snapshot = quote**; `remove` is class X with a
  confirmation that NAMES the binding; `rewrite` is §38 in one verb (`harden` + rebind).
  **It found a silently-inert runtime `comcon.mode()`:** the gating mode is a static set
  once at config load, so an operator calling `enforce()` on a running server got "ok" and
  kept AUDITING — still allowing what they believed they had begun denying. Fixed without
  resetting the counters (the evidence that justified the switch must survive making it);
  per process, no fleet-wide fan-out. `t/comcon_std_ops.t` (26) + 6 controls.
  **Step 3 — `uses`: the first ENFORCED budget (2026-09-12, v5.67).** The mediation
  vocabulary shipped four words while the documents promised ten, and the remainder was
  blocked on exactly that. `comcon.uses(key, limit, window)` attenuates **how many times**
  rather than what: a fleet-wide fixed-window counter in `nginx.shared`, charged on every
  gated operation (a redacted read is free), denied as `budget.uses`, logged-and-allowed
  in audit mode. Measured `+0.037 µs` per charged use — affordable *because* HOST-PERF
  fixed the store first (the same charge would have cost up to 0.42 µs a day earlier).
  `t/comcon_budget_uses.t` (16) + 4 controls; **workers=4, spent=10 of a limit of 10**.
  **Step 4 — `ttl`: a capability LIFETIME (2026-09-13, v5.74).** The other half of TM-2's
  lease: `include()` binds grants at admission, so without a lifetime a fragment outlives
  the mapping that authorised it. `mediate(cap, ttl(s))`, clock starting when the capability
  crosses into the compartment, denial `cap.expired`, audit-first like every gate — and
  **lifetimes compose by `min` where budgets refuse**, because lifetimes are ordered and
  budgets are not. `t/comcon_cap_ttl.t` (11) + 3 controls.
  **`allowHosts` SHIPPED 2026-09-13 (v5.85)** — see the position note above; the blocker was
  not just "no outbound capability" but that fragment invocation is synchronous.
  Remaining *(as of v5.97; the canonical list lives in the POSITION block and is enforced by
  enumeration check [8])*: `opaque.*` (engine-substrate track, never a mediation),
  `std.postures.*` (a decision plus member-level facet attenuation), and the "raw operators
  withheld" governance half. The posture FIELDS shipped at v5.91, `cosign` at v5.88 and
  `protocol` at v5.90 — this sentence named all three as absent until v5.97.
  *(Original scope, preserved:)* The user-facing
  surface is not the kernel but the combinators: `std.profiles.*` (tenant,
  pure_library, forensics/REL, marketplace, config_builder…) and the mediation
  vocabulary (`routes/allowHosts/uses/ttl/window/cosign/readOnly/redact/protocol/
  opaque.*`). Authored *in* the policy language once M3 exists; governed by its own
  policy (raw operators withheld); interceptors that close over capabilities need
  **certification criteria** (TCB-adjacent). Seed set specified at M2.5; grows with
  M5/M6. *(v4.1)* Includes **`std.ops`** — the comconctl verbs as library programs
  over the kernel + ops-resource caps (FOUNDATION §8a): there is no management plane,
  so the "tooling track" of §5.2 collapses into this library plus one thin shell
  (pilgrim substrate: the P19 admin-shell / nginx.repl machinery). *(v4.1, WASM note)*
  Also a future **`wasm` facet**: WASM slots into the one-pattern model as another
  governed language instance — its validation *is* admit, its import object *is* an
  environment (grant by another name), wasmtime fuel *is* budget mediation — giving
  polyglot (Rust/Go) or CPU-heavy leaf fragments a home as admitted, budgeted,
  mediated compute capabilities. COMCON stays the authority plane; WASM never becomes
  a second management surface. Low priority; design note in the memory branch §17.
  *(v5.2 — the provenance ruling:)* the facet's decision rule is now official:
  **substrate follows provenance, not language** — WASM is a *trust* tier, not a
  performance tier. Code born in our world as JS runs T1/T2 (the type-checker +
  compiler is the trust root); JS→WASM is ruled a **category error** — a second
  sandbox around an already-safe language, paying §17's boundary inversion for zero
  trust gained. Code born elsewhere enters as WASM (the validator is the trust
  root), with two execution lanes: an embedded runtime for cold/rare modules, and
  **wasm2c ingestion** for hot ones — foreign WASM lowered to C (its SFI bounds
  checks preserved in the generated source) and fed through the *same* TCC/GCC
  pipeline, `.so` loading, gas and revocation story as maxim output, so everything
  funnels through one trusted path ("C emitted by a tool we trust") with two
  provenance front-ends: maxim JS→C, wasm2c WASM→C. *(v5.3 — C3:)* "same gas story"
  is made literal: wasm2c emits SFI bounds checks but **not** fuel checks, so the
  **back-edge gas instrumentation and V6's CFG check apply to all C entering the
  funnel — wasm2c-emitted as much as maxim-emitted** (else a hot foreign module spins
  unmetered: R4's bug, one lane over). Honest cost: this lane moves wasm2c into the
  TCB beside maxim. Plus one **export lane**: maxim emitting WASM
  carries a COMCON-authored fragment onto foreign hosts (Proxy-Wasm layers, edge
  runtimes) — admission-time guarantees (types, contract tests, free-name manifest)
  travel with the artifact; the authority discipline degrades to the foreign host's
  coarse permission ABI, and the artifact's report says so — and strategically, every
  foreign extension ABI thereby becomes **a target of our toolchain rather than a
  competing model** ("author in typed JS under COMCON; deploy natively on pilgrim;
  deploy as WASM everywhere else" — a distribution story the WASM-first platforms
  cannot offer in reverse). Three closing observations that complete the ruling:
  (i) **WASM natively enforces the possession axiom** — a module has no ambient
  authority at all and can touch nothing beyond its instantiation-time imports, so it
  is *born-bound by construction*; of the four kernel operators, the substrate gives
  us possession for free and COMCON supplies the policy. (ii) **WASM's built-in
  advantages were already bought back for the native tier**: R3's generation checks
  and R4's back-edge gas are exactly the meterability and revocability WASM provides
  intrinsically — engineered into maxim's output so our-born code never needs the
  sandbox. (iii) **A third lane, tiering by heat**: a long tail of thousands of tiny
  *cold* tenant fragments wants per-instance memory caps and instant reset, not a
  per-tenant native `.so` — for our-born code that is QuickJS compartments (S2) with
  per-compartment caps; WASM instances play that role only for foreign code. WASM is
  the border crossing, never the interior.
  *(v5.1 — E1:)* Includes the **dependency workflow** — the #1 tenant question: a
  lockfile-driven `comconctl install` that admits each npm dependency as a
  `pure_library` child fragment with a static-harvest-generated candidate policy,
  pins = lockfile hashes, transitive deps as child cages (v2 §9.5's supply-chain
  inversion, finally given its tooling and manual chapter).

- **HOST-PERF — the two measured host-path costs. ✅ BOTH FIXED 2026-09-12** *(tracked 2026-09-12,
  user decision: track it, do not promote it)*. Both were found by the M5 evidence run
  (§POSITION and `t/tools/host-call-cost.t`), and both are worth fixing on their own terms —
  they are live costs on the rate-limiting path today, independent of whether M5 is ever
  taken up:
  - **`shared.incr` is a linear scan.** Up to 256 slots, comparing 128-byte keys under a
    spinlock (`ngx_js_shared_fn_incr`). Measured **0.079 µs** on a near-front hit and
    **0.417 µs on a miss** against **0.034 µs** for a typed slot stub — and a miss is what a
    rate limiter does for *every new tenant key*. Fix = an index/hash instead of the scan.
    Watch the two properties the scan currently provides for free: expiry reclamation, and
    first-free-slot reuse after a delete.
  - **`req.headers` re-materializes the surface on every access.** Measured **0.130 µs**
    against **0.030 µs** with the surface hoisted to a local, so ~0.10 µs per access is the
    materialization rather than the lookup. Fix = cache the surface per request. Until then
    `var h = req.headers` in a hot handler is a free ~4× on that access, which is worth a
    line in the manual either way.

  **RESULT (A/B measured back to back on one box, same session, 2M iterations per arm):**

  | per call | scan | hash table | |
  |---|--:|--:|---|
  | `shared.incr`, 17 keys | 0.077 µs | **0.062** | 1.24× |
  | `shared.incr`, 217 keys | 0.077 µs | **0.064** | 1.20× |
  | **miss, 217 keys** | **0.420 µs** | **0.084** | **5.0×** |
  | miss ÷ typed slot stub | 12.73× | **2.62×** | the cliff is gone |
  | `req.headers['x-tenant']` | 0.130 µs | **0.043** | 3.0× (hoisted: 0.029) |

  The store is now an **open-addressed hash table with linear probing**, and deletion
  **shifts the cluster back** rather than leaving a tombstone — tombstones would
  accumulate under exactly the workload this fixes (a rate limiter churning keys), and
  clearing them needs a 166 KB compaction under the spinlock every worker shares. The two
  properties the old full scan gave for free are kept: an expired entry is reclaimed when
  it is probed, and a freed slot is reusable immediately. `req.headers` is materialized
  once per request and held on the request's opaque — with the consequence, now
  documented, that a write to it is visible to a later read (it used to vanish into a
  throwaway).

  **What the numbers say about M5.** The precondition is discharged: re-measured, a host
  call costs 0.062 µs where a typed stub on a resolved slot costs 0.032 — so the gap a
  typed ABI could close is now **~0.03 µs per call**, not the 0.38 µs the linear scan was
  contributing. The dominant remaining pieces are the JS→C dispatch (0.038) and the
  spinlock (0.038 → they overlap), neither of which lowering JS removes. *A named,
  measured residual:* `incr` still stores its counter as a STRING and does `ngx_atoi` +
  `ngx_snprintf` per call (~0.024 µs of the 0.062) — a numeric entry would remove it, and
  that is a bigger change to the store's semantics than this one, so it is recorded rather
  than smuggled in.

  Tests: `t/js_shared_hash_table.t` (16 — a MODEL oracle over 4000 mixed operations, plus
  capacity, slot reuse and expiry-inside-a-cluster) and
  `t/js_request_headers_cache.t` (10 — identity within a request, and isolation across
  requests including keepalive, because a per-request surface that outlived its request
  would hand one client another's `Authorization` header).

- **M-SES — engine hardening.** Phases S1–S6 and the gate as specified in
  `HARDENING.md`. Does not block M2–M5 (trusted code); **gates M6/M7-with-tenants**;
  required even pure-AOT (hybrid fallback re-enters the interpreter). Start S1 early.

- **M3 — COMCON front-end.** Restricted-subset parser + capability declarations +
  compile-time rejection of out-of-environment references. Typed AST out, no codegen.
  *(v4.2, §20)* Deliverable added: the **typed-profile spec** — typed-JS is a *profile
  of* policy-JS, never a separate language (a grammar fork would break JS-stays-pure,
  the POM 1:1 mapping, and erasure soundness). Three type layers, none grammar:
  (i) the M2 schema types the API (types arrive from the environment); (ii) inference
  covers locals (M4 — the Misty-like core is small precisely so inference works);
  (iii) residual annotations use an **erasure-sound carrier: JSDoc-style comments**
  (`/** @type {int} */` — valid JS by construction; checkJs/Closure precedent). The
  spec names the profile and fixes the annotation convention + inference boundary.

- **M4 — Type binding.** Bind the AST against the M2 schema → fully-typed IR; `any`
  forced to the hybrid path. Typed locals inferred; mismatches rejected. *(v4.2, §10)*
  Joint M3+M4 output = the **fragment artifact**: standard QuickJS **bytecode** (tier-1
  executable, produced by type erasure — erasure soundness, §10) + a **type/capability
  side-table** keyed by provenance/bytecode offsets + environment signature + content
  hash + admission certificate. One format serving the interpreter, maxim, the
  sign/cache pipeline, worker shipping, and M8's refinement testing.

- **M5 — maxim lowering (one handler).** Consume the **fragment artifact** (maxim's IR
  is already QuickJS bytecode — the side-table's declared types feed its existing
  typed-vars machinery, **replacing inference**; the M5 question is that mapping, not
  "build a lowering"). Emit unboxed C against the typed stubs (the M1 hand-written
  ABI). Deliverable: a compiler-produced `.so` for the M1 example; behavior
  byte-identical to the interpreted rule; the C ideally ≈ M1's hand-written C. *Scope
  note (showcase lesson §5.5):* lowering must **partially evaluate static mediations**
  — a constant-predicate membrane (e.g. an allowHosts prefix guard) becomes an inline
  check in the generated C, not a call. Scenario 43's "policy that vanished" is this
  feature; without it the compile-through performance story is untrue for any mediated
  capability. *(v5.1 — E2:)* deliverable added: **tier-transparent stack traces** — a
  compiled fragment's crash maps through the provenance links (C → bytecode offset →
  source line) to a tenant-readable trace, with the redaction rule "your frames
  visible, neighbors' redacted"; specified now because it is miserable to retrofit.
  *(v5.0 — R4:)* generated C must remain **meterable**: loops emit
  **back-edge gas checks** (the interpreter's `JS_SetInterruptHandler` does not cover
  native code — without this, a compiled infinite loop hangs the worker unmetered);
  until back-edge gas lands, the tier-2-eligible profile is **loop-free** (the M1
  shape); and generated C allocates **only through metered stubs** — no raw malloc is
  ever emitted. *(v5.0 — R5:)* **shared mutable slots (flow, table) are the type
  boundary**: the compiled tier reads them unboxed on the side-table's word, so every
  **write from a lower tier into a declared-typed slot is guarded** (the
  gradual-typing boundary discipline) — otherwise a hybrid fragment writing `any` into
  a slot a compiled fragment reads as `int` (unboxed) is type confusion inside native code.

- **M6 — Dispatch + AOT wiring.** The event dispatcher calls the compiled C function
  pointer (per-tenant `.so`, phase-35 precompile); interpreted fallback for the
  non-lowerable. End-to-end compiled policy serving in a worker. *Gated by M-SES.*
  The POM's class-F live-rewrite machinery (epoch broadcast + dirty→fallback→re-AOT)
  lands here, reusing `describe()` and the cfgbus transport. *(v5.0 — R3:)* every
  compiled fragment checks a **per-fragment generation counter at entry** (one
  load+branch): revoking any capability that was **statically baked** into its C bumps
  the generation → the fragment self-demotes to bytecode (where the live membranes
  are) until re-AOT. Revocation therefore has two `describe()`-visible cost classes:
  dynamic-checked (flag, instant) vs static-baked (generation bump + re-AOT).
  *(v5.4 — the two-clocks pin:)* **epoch and generation are distinct clocks and their
  relationship is now fixed.** An *epoch* is a **binding version** (per node; the
  rollback/rewrite unit — changes when the node's own policy or content changes). A
  *generation* is a **per-fragment invalidation counter for baked authority** — and
  crucially it must catch the *transitive* case an epoch cannot: revoking a raw cap in
  library L invalidates every compiled fragment that baked in a facet *derived from*
  L, even though those fragments' bindings (epochs) never changed. Mechanism:
  revocation **fans out at revoke time through the provenance/grant-chain registry**,
  bumping the generation of every fragment whose baked caps derive from the revoked
  root — push once at the rare event, so the hot path stays one load + one branch at
  fragment entry. The two clocks co-trigger re-AOT but answer different questions:
  "did my policy/content change?" (epoch) vs "did authority I baked in change?"
  (generation).
  *(v5.0 — R10:)* multi-node changes: **partial application of a meet is itself a
  meet** — restrictive overlays are safe in any order/timing (the monotone-rollout
  property; why the lockdown button needs no transaction); **widenings** (administrative
  rebinds) use **two-phase epoch groups** — prepare on all workers, flip together.

- **M7 — Benchmark the real pipeline.** Compiled-by-COMCON policy vs interpreted vs
  stock, same method as M1. The number that confirms (or corrects) M1 at
  full-pipeline scale — expected between M1's 28% and 96% endpoints, near the top if
  the lowering is direct. *(v4.1)* Add a **WASM baseline column**: the same count+tag
  policy as a Proxy-Wasm filter (ngx_wasm/wasmtime), measuring the host-boundary
  marshaling cost against maxim's borrowed-`ngx_str_t` stubs — our claim that WASM
  pays at exactly the boundary this workload hammers is currently cost-model
  reasoning, and M7 is where it becomes (or fails to become) a measurement. *(v4.2)*
  Also a **same-artifact tier row**: one fragment artifact measured on its bytecode
  (T1) and its C (T2) — the per-fragment version of M1's 28%/96% endpoints.

- **M8 — Safety hardening + audit. GATE for any multitenant use.** The compiler-
  faithfulness obligation, now precise (SEMANTICS §3 assumption F): *the lowered C
  must refine the mediated semantics along the provenance links — simulating every
  MEDIATE/NAME gate it erased.* *(v4.2)* Practical method: **T2-refines-T1 differential
  testing** — run each fragment's admission allow-suite on both tiers of the same
  artifact and compare; the contract tests double as refinement evidence. Plus
  per-tenant `.so` isolation, table-key namespacing, worker resource guards,
  threat-model doc.

- **M9 — Breadth.** More events (response headers, L4/TLS), the borrowed/owned string
  ABI to kill refcount churn, the `any` hybrid, wider typed surface. Iterative.
  *(v5.4)* **Adaptive profiles land here** — the core through M8 is restrictive-only
  (unconditionally-ACI composition; FOUNDATION §7); transforms, mirror-as-policy, and
  scenario 22's codemods arrive as an isolated addition with the already-specified
  one-per-node rule. **Post-M9 track:** information-flow/taint labels (confidentiality
  axis — FOUNDATION §13.4).

**Critical path:** M1 ✅ → M2(+S4) ✅ → M2.5 ✅ → M3 ✅ → M4 (admission half ✅, typed IR for
lowering NOT started — banked 2026-09-11) → M5 → [M-SES gate ✅ — S1–S6 built, **audit
signed 2026-09-12** with §3's five gaps accepted as residual risk] → M6 → M7 → M8 gate → M9.

> The chain is no longer blocked on capability anywhere before M5, and the M-SES gate is
> signed off. It is blocked on ONE decision: whether to commit to M5, which is what makes
> the typed IR worth building.

---

## 3. Delivery staging (v2 §8.1, preserved)

The three-stage delivery order stands, now with its guarantee proved rather than
argued (soundness is stage-independent — SEMANTICS §3):

1. **Stage 1 — declarative only.** Policy code executes only inside compilation
   episodes; runtime = engine consults static residue. Covers the multi-tenant core
   and is exactly the maxim-compilable profile. Stage-1 reservations (tri-state
   descriptors, check-point placement, function-tolerant field shapes) remain
   mandatory so stages 2–3 arrive without migration.
2. **Stage 2 — compile-time transforms.** Parser callbacks and source/AST rewrites —
   mirror-as-policy (adaptive profile) lands here.
3. **Stage 3 — runtime interceptors.** The resident residue: value validators, dynamic
   predicates — `mediate` interceptors on the hot path, tri-state keeping allow/deny
   fast.

---

## 4. Minimal first slice (v3 revision of v2 §11) — the engineering seed of increment A (§13)

Exercises every pillar, updated for the kernel and the anchors model:

1. **Anchor recognition** ✅ *(D5b-2, 2026-09-12)* — `"use comcon: <name>";`
   directive-prologue anchors parsed and exposed as node attributes (inert in plain JS),
   queryable as `anchors(glob)`. `t/comcon_pom_anchors.t`.
2. **Minimal kernel** — `env()/grant/bind` for one enumerated node kind; a policy
   program runs at stage 0 under a hardcoded root environment.
3. **One static enforcement** — a free identifier not in the bound environment is a
   compile error; an excluded production (`WhileStatement`) rejected via `admit`'s
   syntactic predicate.
4. **One runtime enforcement** — a single mediated capability (prefix-guarded `fetch`
   or an opaque value) enforced at a check point.
5. **Monotonic nesting test** — an inner `bind` can tighten but not loosen (2)–(4);
   plus the two negative tests from the worked examples: granting a withheld name
   fails statically; splicing a capability into a quotation fails at construction.
6. **Escape-probe seed** — the first five S6 probes (constructor ladder, prototype
   pollution, indirect eval, `.stack` leak, COM upward traversal) running against the
   slice.
7. **Microbenchmarks** — shared-property access, cross-fragment call, mediated-op
   dispatch vs stock qjs; the COW/IC/revocation cost question (FOUNDATION §13.1) is
   the make-or-break implementation risk and must be measured, not argued.

> Validates: anchors + external binding, policy-as-program over the real kernel,
> static + runtime enforcement, the narrowing lattice, the cap-free-quotation rule,
> and the first hardening probes — a stage-1 artifact honoring the stage-1
> reservations.

---

## 5. Lessons from writing the showcases (2026-08-19) — plan deltas

Writing the 45 scenarios was itself a design probe. What it surfaced, and what changed:

1. **The standard library is the product surface.** Users see combinators
   (`routes/uses/ttl/window/redact/protocol/opaque.*`), almost never the raw kernel.
   → **M-LIB added** (§2), incl. certification criteria for cap-closing interceptors.
2. **Ops tooling carries half the scenarios** (learn/shadow/enforce, diff, revoke,
   epoch rollback, forensics attach, evaluate, docs, trust-report). → named as
   deliverables inside milestones rather than a monolith: deny-trace + diff with
   M2.5's schema; shadow/audit = the binding failure-semantics option (near-free);
   revoke/cascade with M-LIB mediations; epoch rewrite/rollback with M6; learning-mode
   static harvest = stage-1 dry-run (already in staging plan).
3. **Denial/explain schema promoted** from open question into M2.5 (it is the UX, the
   deny-suite assertion language, the learning record, and the LSP diagnostic).
4. **Selector language promoted** from open question into M2.5 (used constantly,
   invented ad hoc while writing — the clearest "spec missing" signal).
5. **M5 must partially evaluate static mediations** (scenario 43) — scope note added.
6. **Opaque values + COW domains have no milestone home** (scenarios 7/16/19/32
   depend on them; the M-track builds the compiler, not this engine substrate).
   → recorded as an explicit **engine-substrate track** decision point after M2.5:
   either schedule it (stage-1 declarative COMCON needs it) or mark those scenarios
   phase-2. The COW/IC microbenchmark in the first slice (§4.7) is its go/no-go input.
7. **Cluster-edge policing (scenario 27) crosses process boundaries** — the kernel
   semantics is single-runtime; workers-as-fragments is genuinely new design surface.
   → added to FOUNDATION open questions; deferred.
8. **Confirmations (no change):** determinism-caps-early paid off twice (36, 44);
   meet-composition (21, 45) and epochs (24, 28, 41) required no new mechanism — the
   kernel absorbed every scenario that wasn't an explicitly-listed gap.

### Scenario → earliest-home traceability

| Capability cluster | Scenarios | Earliest home |
|---|---|---|
| include/admit/bind pipeline, grammar-valued interfaces | 1, 3, 4, 30, 44, 45 | M2.5–M4 (+M-LIB facets) |
| Intensional queries, quotation/realize, pin-by-hash | 38, 39, 40 | M2.5 (spec) / M3 (engine) |
| Compile-through + membrane partial-eval | 43, 33(economics), epilogue | M5–M7 |
| Live epochs: revoke, fuses, overlays, rollback, rewrite | 2, 12, 21, 24, 28, 31, 34, 41 | M6 (+M-LIB mediations) |
| describe()-derived docs/audit/trust | 29, 37, 25 | M2 registry + tooling verbs |
| Learning mode / shadow | 5, 14 | bind failure-semantics (early) + stage-1 harvest |
| Frozen intrinsics, budgets, REL/forensics | 18, 10, 13, 6 | M-SES S1/S5 (+M-LIB profiles) |
| **Opaque values + COW views** | **7, 16, 19, 32** | **engine-substrate track — UNSCHEDULED (lesson 6)** |
| Multi-language includes, adaptive transforms | 11, 15, 22, 36 | M9 / stage 2 |
| Cross-process, mobile fragments, marketplace ops | 27, 17, 8, 9, 20, 23, 26, 35, 42 | mixed; 27 = new open question |

The table doubles as a demo-driven acceptance checklist: a milestone is "showcase-true"
when its scenarios run as written.

## 6. Lessons from the user's manual (2026-08-19) — second working-backwards pass

`MANUAL.md` (written as-if-shipped) surfaced a *different* class of gaps than the
showcases — the user-journey ones (full harvest: MANUAL.md Appendix B):

1. **Tenant-side SDK is missing from the plan entirely.** `comconctl dev` — run a
   fragment against **capability doubles** generated from the same environment spec the
   host binds, with the emulator and production admission being the *same* `admit`
   gate ("works locally" ≡ "admitted"). → new work item alongside M-LIB; the doubles'
   fidelity contract is its design question.
2. **The denial schema needs stable machine codes** (`E_CAP_*`, `E_ADMIT_*`,
   `E_BUDGET_*`, `E_PIN_*`…) — tenants pin CI to codes, not message text. Sharpens the
   M2.5 denial-schema deliverable.
3. **The widening workflow is a product surface:** `comconctl request` → host reviews a
   descriptor diff → approval = new epoch → generated docs self-update. The lattice
   makes self-service *narrowing* safe; the manual shows requesting-more must be a
   first-class (reviewed) flow, not an email.
4. **Terminology freeze at M2.5** — the manual had to pick user-facing words
   (cage/binding/epoch/facet/pin); its glossary should become normative before more
   docs accrete synonyms.
5. **Product decisions flushed out as [TBD]s:** default-root out-of-box contents
   (secure-vs-useful line), budget unit semantics (wall vs CPU, per-request vs
   per-episode), grammar-version compatibility window on engine upgrades, nginx
   integration knobs.
6. **Confirmation:** the grant-little → watch-denials → adjust → enforce loop is the
   product's single repeated motion across all three hats — the tooling deliverables
   of §5.2 should be sequenced to make *that loop* work end-to-end first.

## 7. Adoptions from Crockford's Misty (2026-08-20)

Reviewed mistysystem.com (same E-lineage ocap tradition as COMCON — its
authority-by-creation/construction/introduction is our possession metatheorem; its
facets/revocation are our mediate flavors — so mostly *validation*). Four targeted
adoptions, all cheap:

1. **M3 subset shape:** define the authored typed profile starting from a
   "Misty-like core" of JS — no `this`, no classes/`new`/prototype access, no
   coercion/truthiness. Cuts M3–M5 surface substantially; deletes prototype-authority
   leaks from authored code by construction. (External hardening still takes full JS.)
2. **M-LIB: Misty-style patterns** as the validation vocabulary for grammar-valued
   interfaces (named fields, composable, bounded quantifiers). Two bonuses: ReDoS is
   impossible by construction → tenant profiles can deny the regex engine and offer
   patterns instead (a removed vulnerability class, like scenario 3's SQL); and
   static patterns compile through (maxim lowers them to straight C).
3. **M-LIB: `stone()`** — deep immutability for plain *data*, paid once, zero
   per-access cost; the cheap alternative to read-only membranes on hot shared paths
   (membranes for authority, stone for data).
4. **Guest posture (M-SES S4):** actor/worker creation is a capability,
   default-denied in tenant profiles (Misty: "guests cannot create actors") —
   sharpens the existing Worker-constructor de-ambient item into a stated default.

Also: Misty's actor model (no shared memory; the address IS the capability) is the
recommended direction for open question 8a (cluster edges); and Misty + ADsafe join
the patent prior-art list — they confine by language replacement/authored subset,
COMCON confines unmodified JS via external policy over a program tree, compiled
through. NOT adopted: Misty as source language, new syntax, DEC64 (violates the
JS-stays-pure thesis).

## 8. The symmetry correction (v4, 2026-08-20 — user-spotted)

The v3 stack governed the program at two hinges (typed surface → POM; caps/classes on
the tree) but governed config at only one (caps/classes on COM) — the config *surface*
was untyped and unadmitted. The correction (FOUNDATION §2a): **one governed-language
pattern, N instances**; **data is code bound to the empty environment** (Principle 9);
and COM gains its admission hinge as work item **M-CFG**. Plan impact: M2 declared
dual-role; M2.5 gains instance-genericity + ∅-env + rights-meet; M-CFG added as a
parallel track; nothing measured or gated moves. Retro-evidence: the snapshot/rollback
JSON files were already quotations of COM subtrees, and the operator-reconfig UX was
already closure-vs-quotation for the config instance — the mechanisms existed before
the name. Patent note: "config as sentences of a restricted, typed language admitted
under a schema" is a claimable refinement between the COM claims and the safety-class
claims.

## 9. The comconctl closure (v4.1, 2026-08-20 — user-spotted)

comconctl looked like "a utility using some API" — an unclosed generalization. Closed:
**there is no management plane** (FOUNDATION Principle 10 + §8a). Every verb is a
`std.ops` library program run as an admitted episode in an operator session; all ~20
verbs decompose over the kernel + the ops-resource capabilities (the new third closed
enumeration, an M2.5 deliverable). Plan impact: the §5.2 tooling deliverables collapse
into M-LIB/std.ops + one thin shell (P19 substrate); **M-SES scope shrinks** — no
separate admin API to harden, administration rides the same admitted-episode gate as
everything else; audit closes over operators (trust-report on sessions;
office-hours/cosign mediate admin verbs natively). Residue outside the language:
bootstrap + transport (host integration).

## 10. Compilation tiers & the fragment artifact (v4.2, planning session)

The question "compile typed JS to an interpreted subset first, or directly to C?" has
a structural answer: **those are not sequential stages but two permanent tiers.** The
interpreted tier (bytecode) is load-bearing forever — (1) the phase-34 hybrid fallback
*runs* it during class-F rewrite windows; (2) the `any`/dynamic residue executes on it;
(3) admission and `comconctl dev` run interpreted (no C toolchain at admit); (4)
maxim-less deployments are the correct-but-28% tier. Direct-to-C is therefore only
ever *additive*, and maintaining a second backend that must agree with maxim doubles
the M8 burden — rejected.

**Deciding fact:** maxim's IR *is* QuickJS bytecode (its pipeline is bytecode → C →
GCC/TCC). Hence the chosen architecture — **"fat bytecode"**:

```
typed policy-JS ──M3/M4──▶ FRAGMENT ARTIFACT ──(iff maxim)──▶ C → .so
                            = bytecode                         phase-34 hybrid:
                            + type/cap side-table              {C fn, bytecode},
                            + environment signature (= the      prefer C, fall back
                              fragment's free-name manifest)
                            + hash + cert
                            + SCHEMA HASH (v5.0 — V2)
```

*(v5.0 — V2)* The artifact records the **hash of the schema version it was admitted
under**, and the loader **verifies compatibility at every load**: a registry/engine
upgrade that changes an op's type or effect class makes stale cached artifacts fail
loudly into re-admission (old epoch keeps serving, per R7) instead of serving with
stale assumptions baked into their C. Pin-by-hash protects against *content* drift;
this protects against *schema* drift. *(v5.3 — C11:)* schema-hash pinning is
**per-instance**: config-fragment artifacts (M-CFG) and wasm-facet admissions carry
and verify it exactly as program fragments do.

**Erasure-soundness principle:** a typed program run interpreted with its types
ignored behaves identically to its compiled form — types only *reject* (at admission)
and *accelerate* (at tier 2), never change semantics. So "typed → interpreted subset"
is type erasure plus the profile check, not a translation; and M8's obligation becomes
the checkable "T2 refines T1" (differential testing on the same artifact). M1's
numbers are the two tiers measured: 28% (T1) / 96% (T2 ceiling).

Parked as a post-M7 optimization: a **typed-IR entry point inside maxim** (bypassing
its bytecode-decode/type-recovery front phases for fully-typed fragments) — better
type precision, same back phases, still emits the bytecode sibling.

## 11. The v5.0 design-review hardening (R1–R12, 2026-08-22)

A full adversarial pass over v4.2 before implementation, adopted in whole (user
decisions: R1 = one-adaptive-per-node; R6 = as proposed). Summary and homes:

| # | Issue found | Fix | Home |
|---|---|---|---|
| R1 | meet/ACI claim false for adaptive transforms | confluence restricted to restrictive; **≤1 adaptive policy per node** (`E_BIND_ADAPTIVE_CONFLICT`) | SEMANTICS (BIND), FOUNDATION §4 |
| R2 | cap-free deep check TOCTOU-unsound (getters/proxies/mutation) | QUOTE side condition = **stone** (deep-frozen plain data); opaque values unspliceable | SEMANTICS (QUOTE), FOUNDATION §6 |
| R3 | revocation invisible to compiled fragments (membranes erased) | per-fragment **generation check at entry** → self-demote to bytecode; revocation cost classes in describe() | M6, PERFORMANCE |
| R4 | tier-2 escapes gas/memory metering (interrupt handler = interpreter-only) | maxim emits **back-edge gas**; loop-free profile until then; allocation via metered stubs only | M5, HARDENING S5 |
| R5 | type confusion via shared slots (lower-tier `any` write → unboxed read) | declared-typed slots get **write guards at the tier boundary** | M4/M5, M8 audit |
| R6 | realize = confused deputy (operator's full env) | **least-authority realization**: mandatory contract + free-name manifest ∩ realizer grants | SEMANTICS §4.4, MANUAL |
| R7 | unbound-node execution & pin-mismatch semantics undefined | EXEC requires binding (`E_UNBOUND`); **bound nodes never unbound**; pin mismatch refuses new epoch, old serves | SEMANTICS, POM |
| R8 | positional ids silently retarget after structural edits | **creation-ordered ids**; positional selectors volatile; **binding-set drift report** on admission | POM §2 |
| R9 | snapshot query bindings let new code escape policies | **born-bound rule**: match-sets recomputed at every admission in reach | POM §2 |
| R10 | mixed-epoch windows during multi-node changes | monotone-rollout property (partial meet = meet) for restrictive; **two-phase epoch groups** for widenings | M6 |
| R11 | admission front-end is pre-sandbox attack surface (parser CVEs) | fuzz the front-end; resource-limit + isolate admission parsing | HARDENING S6 |
| R12 | failure-mode composition undefined | strictness order `reject > deny > attenuate > audit`; meet takes strictest | SEMANTICS (BIND) |

Non-technical: repo confirmed **private** (company-internal) — pushed design docs are
not public disclosure; keep non-public until the patent-filing decision.

## 12. The verification track (V1–V15)

A second review pass asked, for every claim: *what would convince a skeptic?* The
resulting verification obligations live in **`VERIFICATION.md`** — two were design
decisions adopted immediately (V1 numeric model: JS-double semantics normative,
SEMANTICS §6; V2 schema-hash pinning at artifact load, §10 above); the rest attach to
milestones as a V-column:

| Phase | Verification deliverables |
|---|---|
| now / M2–M3 | V3 ✅ executable reference semantics (kernel oracle) · V4 ✅ monotonicity-as-assertion · V7 ✅ generated (never maintained) enumerations (all 2026-09-12) |
| M5–M6 | V5a per-artifact translation validation for the loop-free profile · V6 gas-placement CFG check on emitted C · V8 schema conformance tests (generated per registry row) · V9 drift-check extended to POM ops · V13 erasure spot check |
| M7/M8/M-SES | V5b coverage-guided differential fuzzing · V10 TLA+ model of the epoch/two-phase protocol (incl. worker crash mid-flip) · V11 ✅ **policy mutation testing** (widen-one-permit mutants must be killed by the deny-suite) · V12 ✅ golden denial-code corpus — *both built early, 2026-09-12; V12's finding — that the ADMISSION refusals had no codes to pin to — was closed the same day by [TBD-2] (v5.62), which the corpus now freezes too* · V14 reproducible builds · **V15 ✅ the assurance case (claim → assumption → evidence; the umbrella) — BUILT 2026-09-12 as `ASSURANCE.md` + `t/comcon_assurance.t`, unsigned** |

## 13. The engineering review (E1–E12) and the increment re-cut (v5.1)

A third review pass — ergonomics, sustainability, reuse, incremental value — with three
user decisions (2026-08-23): **E5** merge maxim into the vendored tree (→ M-UNIFY);
**E6** docs-v5.0 is frozen as **the single normative spec** — in-place revisions with
changelog entries from now on, full new sets only at genuine reframes; **E12** adopt
the increment re-cut below.

**The re-cut (E12).** The milestone chain is compiler-first, but the *value* order is
confinement-first: the highest-demand capability — multi-tenant confinement on the
interpreted tier — needs no compiler at all. Milestones remain the engineering tracks;
**increments** are the shippable cuts across them, each "showcase-true" for a named
scenario set:

| Increment | Contents | Showcase-true for | Compiler? |
|---|---|---|---|
| **A — COMCON-lite** | S1+S2, registry allow/deny bitmaps, denial log, audit→deny loop | 1, 2, 5 (partial), 18, 38 (audit-mode) | **no** |
| **B — onboarding** | learning-mode static harvest, generated docs, dependency workflow (E1) | 5, 25, 29 | no |
| **C — typed + compiled** | M-UNIFY, M3–M6, fragment artifact, tiers | 43, 48, 49 | yes |
| **D — live POM ops** | queries v1 (E9), rewrite/epochs | 38, 41, 42 | partially |
| **E — config instance** | M-CFG | 36, 46, 47 | no (parallel anytime) |

**Dogfood at increment A:** the first tenant is ourselves — a mirror demo (e.g. A2.8)
running caged under COMCON-lite. Cheapest ergonomics verification that exists.
*(v5.3 — C2, consistency fix:)* scenario 7 (opaque secrets) was wrongly listed under
increment A — opaque values sit on the unscheduled engine-substrate track with
16/19/32 (§5.6); A's disclosure story is covered by 38 in audit mode instead.

*(v5.4)* Two integration deliverables pinned to increment A: (1) **the nginx
integration reality check** — confirm v2 §9.4's contract against the actual codebase;
(2) **TM-2, session identity → environment mapping** (THREATS.md), riding the
`nginx.repl` substrate — before dogfood.

*(v5.5 — the reality check DONE, `INCREMENT_A.md`):* verdict — the §9.4 contract
mostly holds (authority does fall on property/method lines for the *mutation* surface;
`r.location`'s deliberate `srv_op=NULL` defanging is the precedent to generalize), and
the QuickJS factoring is favourable (classes registered once per runtime, prototypes
per context ⇒ per-tenant method-subset compartments need no class surgery — S2
confirmed). But it reordered the build: **(A1) owner-field the three process-global
handle registries first** — the `sock→listener→serverByName→server→addLocation` reach
cycle is mediated by ownerless global arrays that separate contexts do NOT isolate;
this is higher-leverage than context-splitting and independent of it. **Four
omnipotent, un-property-gateable members** (`config.write`, `nginx.repl.eval/listen`,
`nginx.use/install`, `Worker`/`SharedWorker`) are the concrete content of S3's
withhold-by-default. Two confinement bugs to fix in A1: script-writable
`workerMemoryLimit`/`Timeout` (a tenant raises its own cap) and the flat un-prefixed
`nginx.shared` (pulls M8's table-key namespacing earlier). Full task order A0–A4 +
file:line anchors in `INCREMENT_A.md`.

*(v5.6 — increment A is UNDER CONSTRUCTION, most of it built:)* the identity seam,
the reach gates, the deny-by-default tenant environment, host→tenant grants, and a
**confined tenant serving live HTTP requests** are implemented and tested
(`t/comcon_*`; build log `INCREMENT_A.md` §6). *(v5.7)* A4 done too
(denial log w/ TM-1 quotas, audit→enforce mode, tenantDenials() report). Remaining:
the dogfood demo (acceptance); multi-tenant and request-facet grants follow.

**Compatibility principle (write it once, honor it forever):** *pilgrim without COMCON
remains fully supported; COMCON attaches per-fragment; there is no flag-day.*

Remaining E-findings folded elsewhere: E1 dependency workflow (M-LIB), E2
tier-transparent stack traces (M5), E3 "tenants see profiles, never the kernel" (a
standing doc-review criterion), E4 `comconctl docs --json` for generators (std.ops),
E7 two-lane CI (VERIFICATION), E8 the first slice doubles as the new-engineer
onboarding exercise, E9 selector staging (M2.5), E10 one generator/two outputs
(VERIFICATION V8), E11 stage 1 needs no generic membrane machinery — C-side bitmaps +
JS closure facets suffice; the transform membrane is stage 3 (§3).
