# M-SES audit checklist (S6)

**Status: SIGNED 2026-09-12 (§5), engineering and security — BUT BOTH ROWS CARRY
THE SAME SIGNER, so this is one attestation, not two. No second pair of eyes has
reviewed it. The security row accepts §3's five gaps as residual risk; it does
not close them.** Evidence assembled 2026-09-11 and re-measured 2026-09-12 after
fourteen commits of hardening (§2b), then re-run in full on a rebuilt tree for
the signature (the run log is in §5).

An audit certifies a date — if you are reading this well after the one above,
re-run §4 before trusting any row.

**Read the scope cell, not the fact of a signature.** The condition this file
sets is that a reviewer who did *not* write the code signs; the signer qualifies
(the code and tests were written by an AI session under their direction), but the
§4 commands for this signature were executed by that same authoring session
rather than independently reproduced. So the engineering row attests *acceptance
of reproducible evidence*, not independent reproduction, and says so. Every
command is in §4; the negative controls in §5 are what make §2b falsifiable in
minutes.

Companion to `VERIFICATION.md` (which places S6 in the gate ladder) and
`HARDENING.md` (which specifies S1–S6). This file answers one question per
claim: **what would convince a skeptic, and does it exist?**

## 0. Scope and assumptions

- **In scope:** a confined `comcon.include` fragment, both interpreted and
  AOT-compiled tiers, running in an nginx worker.
- **TCB assumed unforgeable:** the QuickJS engine itself, the C host functions
  the fragment may call, and the nginx process boundary. Engine escapes are
  SR-3's subject, not this file's — SR-3 PASSED 2026-09-01 as a pentest, which
  certifies that date and nothing after it. S6 exists because the gap between
  "was true once" and "is true now" is the gap that matters.
- **Out of scope:** availability of the host itself (a fragment cannot be
  prevented from consuming its own CPU budget, only bounded — see (e)).

## 1. The five gate conditions

The M-SES gate: no probe may (a) obtain a value not in its ρ, (b) mutate a
frozen intrinsic, (c) create code from strings without admit, (d) traverse a COM
facet beyond handle reach, (e) escape the resource guards.

**What these rows do and do not establish.** Each probe shows the mechanism
behaves correctly **when it runs**. None of them shows that it *runs on every
path that reaches it*, and that distinction is not academic: the two most
serious defects found in the 2026-09-11/12 hardening were both of the second
kind — a guard that was correct, and a path that avoided it. The declarative
reviewer read comments correctly and a bare CR walked past it; the C3 admission
gate checked correctly and a malformed contract meant it never executed. The
reachability tests added in §2b are cited per row below; where a row has no
such citation, its reachability is **not evidenced**.

| # | Claim | Mechanism | Evidence | Verdict |
|---|---|---|---|---|
| **a** | A fragment cannot obtain a value outside its ρ | free-name gate at admission + no ambient globals in the compartment | `t/comcon_mses_gate.t` probes `a_global_this`, `a_com_root`, `a_comcon` (closed confined, **open unconfined**); `t/comcon_include_admit.t` refuses an ungranted free name; `t/comcon_include_grant.t` asserts `"nginx":"undefined"` inside the fragment. **Reachability:** `t/comcon_include_contract_fuzz.t` — a malformed contract used to skip the admission-time half of this entirely. Two independent mechanisms back this row, and only the second held: with admission bypassed, a fragment naming `nginx` still got `ReferenceError: 'nginx' is not defined`, because the compartment has no ambient globals. | **EVIDENCED** (both mechanisms; admission reachability evidenced since 2026-09-12) |
| **b** | A fragment cannot mutate a frozen intrinsic | M-SES lockdown freezes core intrinsics | `t/comcon_mses_gate.t` probes `b_proto_poll`, `b_array_push`, `b_freeze_str` (closed confined, open unconfined) | **EVIDENCED** |
| **c** | A fragment cannot create code from strings without admit | dynamic-code taming (`%Function%`, generator/async constructors, eval) | `t/comcon_mses_gate.t` probes `c_fn_ctor`, `c_obj_ctor`, `c_gen_ctor`, `c_async_ctor`; `t/comcon_include_mses.t` asserts `routes=closed,closed,closed,closed`. **Reachability:** `t/comcon_include_contract_fuzz.t`. This row is also backed by two mechanisms — a static refusal at admission and the lockdown that removes the constructors from the compartment — and until 2026-09-12 the first was skippable by `include(src, {imports: 42})`. **Measured on the pre-fix build: the second held.** A fragment whose body was `eval("1+1")` compiled, and invoking it threw `ReferenceError: 'eval' is not defined`; `Function` likewise. So the defect was a defence-in-depth failure, not an escape — but the row was marked EVIDENCED while one of its two mechanisms could be bypassed, which is the thing a signer should know. | **EVIDENCED** (both mechanisms; admission reachability evidenced since 2026-09-12) |
| **d** | A fragment cannot traverse a COM facet beyond handle reach | A1 reach gate (`ngx_js_compartment_may_reach`) + route-glob facets | `t/comcon_include_grant.t`: `sock.listener` is null cross-compartment, and prototype planting on a granted handle fails; `t/comcon_com_facet.t`: an out-of-glob location is not visible; `t/comcon_com_facet_mutate.t` (12 checks): gated mutation inside the route succeeds, outside is denied | **EVIDENCED** |
| **e** | A fragment cannot escape the resource guards | per-fragment wall-clock deadline (always armed) + 64 MB cap on the comcon runtime | `t/comcon_mses_gate.t` `GUARD-FIRED` probe (armed guard stops a runaway); `t/comcon_fragment_deadline.t` (a fragment with **no meter** is still bounded) | **EVIDENCED for TIME; PARTIAL for MEMORY — see §3** |

**On (e): the probe found a real hole, it did not confirm a clean state.** Before
2026-09-11 the deadline was opt-in — `__invokeConfined` armed it only when
`contract.meter[...].timeoutMs > 0` — so a fragment with no meter ran unbounded.
Measured: 4474 ms to completion with nothing stopping it. An accidental infinite
loop hung the worker with no escape involved. Fixed in `4fcaca21d`; see
`OPERATOR_API.md` §3.

### Why the probes are believable

Every probe in `t/comcon_mses_gate.t` runs **twice** — inside the confined
fragment and in unconfined host JS — and the suite asserts the two **differ**. A
probe reporting "closed" in both contexts is not evidence of confinement; it is a
probe that never held the capability, and the suite fails on it. Measured:
**12/12 closed confined, 12/12 open unconfined**. Without that control, a probe
that silently stopped working would read as a pass forever.

## 2. Memory safety of the confinement machinery

The C that implements confinement — compartment enter/leave, facet re-wrapping,
grant injection, the JSON marshalling that moves values across the boundary — is
itself attack surface.

| Tool | Corpus | Result |
|---|---|---|
| ASAN | 35 COMCON files, 336 tests | **0 findings** |
| UBSAN | 35 COMCON files, 336 tests | **0 findings in `src/js`**; 35 in stock nginx, all one site (`ngx_pstrdup`, `src/core/ngx_string.c:84` — `memcpy(dst, NULL, 0)` at cycle init; upstream's, benign) |

Re-run: `bash t/run_sanitizers.sh`. Measured 2026-09-11 after the work in §2b.

**What changed in this machinery since the first measurement**, because a clean
sanitizer run over unchanged code is a weaker claim than one over code that
moved: the socket registry gained a per-slot generation and a single install
choke point (`ngx_js_socket_reg_install`), listeners now record the generation
they attached to, and a misaligned 32-bit load in the broadcast receive path was
replaced with `ngx_memcpy`. The first two are lifetime changes, the third was
UBSAN-visible UB. All three are covered by the corpus above plus the fuzz files
in §2b.

**The suites' own `ok - no sanitizer errors` line is VACUOUS** — neither `objs/`
nor `objs_jit/` is built with a sanitizer, so it has nothing to look at. Only the
script above produces a meaningful answer, and it carries four guards for that
reason: the binary must carry the sanitizer's symbols; a positive control must
land a report through the same `prove` pipeline; the run must have executed
tests; and a file skipping with `no js module` means nginx could not start. The
last two exist because the script produced a vacuous PASS on itself twice while
being written.


## 2b. Fuzz corpus — what exists, and what it found

The gap table below used to carry all of this in one cell. It is evidence, not a
gap, and §5 asks a signer to check §3 — which is only possible if §3 is short.

Each file validates its own instrument before reporting (planted bugs,
false-positive controls, work counters), and each fix has a negative control:
reverting it fails the named test.

| Surface | Test | What it found | State |
|---|---|---|---|
| **Admission path** | `t/comcon_declarative_fuzz.t` — 6000 deterministic, index-addressable mutants | A **profile escape**: `reviewDeclarative` ended a `//` comment at LF only, so a bare CR (or U+2028/9) hid the rest of the line from the review while the engine compiled it as code. `a(1); //<CR>for(;;){}` was accepted with a table listing only `a(1)` — the artifact an operator signs omitted a statement. Plus 3 divergences where accepted sources were not valid JavaScript. | **FIXED** |
| **COM setters** | `t/js_com_setter_fuzz.t` — corpus derived from `nginx.describe()`, 344 members, 8600 hostile assignments | Four `describe()` rows misdeclaring their type (`limitExcept`/`keepaliveDisable` are `string[]`; `clientBodyInFileOnly` is tri-state; `directio` is `number\|string`). That surface feeds M4 `reviewCalls` and `describeType()`, so a wrong type is an admission decision on a false premise. **No memory error** under ASAN/UBSAN. | **FIXED** |
| **Socket surface** | `t/js_com_socket_fuzz.t` — 1500 generated addresses + non-string battery + fd-leak oracle | A **use-after-close aliasing bug**: `close()` freed the registry slot while the JS object kept its index, so the next `createSocket()` handed it back and a stale handle read *and closed* an unrelated live socket. The first fix was incomplete — a retired listener holds the same index, and `removeListener(…,{hard:true})` clears the invariant that protected it, so `.socket` **minted a fresh valid handle** to the new occupant. Also NUL truncation and `strtol` laxness in the address parser. Negative results worth keeping: the 48-byte host buffer is correctly bounded, and no fd leak over 600 calls. | **FIXED** (two rounds) |
| **`broadcast()`** | `t/js_com_broadcast_fuzz.t` — 3-worker fleet, saturation oracle | Reached the broadcast RECEIVE path, which nothing had, and **UBSAN reported a misaligned load** on the first run: the header is `[u8][u32][u32]`, so the receiver's `(uint32_t *)(void *)(buf+1)` read both fields off alignment. Sixteen clean sanitizer runs had passed over it — *a sanitizer only sanitizes what you execute*. Plus an uninitialised `st->owner` (`ngx_alloc` is `malloc`, and the ownership gate reads it) and a missing generation bump, both by inspection. | **FIXED** |
| **Numbers cast, not checked** | `t/js_com_numeric_range.t`, `t/js_com_peer_range.t`, `t/js_com_ssl_range.t`, lint `t/tools/numeric-cast-sweep.py` | One defect in nine places. `JS_ToInt32/64` answer 0 for `NaN`, `{}` and `"abc"` with no error, and hand back negatives the caller stores unsigned: `peers[0].weight = -1` stored ~1.8e19 into the load balancer, `ssl.verifyDepth = {}` silently set verification depth to 0, and `respond(-1)` put `HTTP/1.1 18446744073709551615` on the wire. 94 sites of the shape, 81 already guarded, 4 false positives, **9 real**. All now use one shared `ngx_js_com_num_range()`. | **FIXED** |

| **`grantToTenant`** | `t/js_com_grant_declare.t` | Triaged from the 0%-coverage list and it was **neither dead nor working**: it once published a socket into the tenant compartment, the M-CFG convergence removed that compartment, and nothing replaced the read — so it validated a `NginxSocket`, stored its handle, and never looked at it again. Callers were told a capability had been conferred when none had, and `tenantLearning()` then listed the name among its `grants`; a demo in-tree claimed it handed over "a genuine host capability". Reduced to what it actually does — record a NAME for the onboarding delta — with the second argument accepted and ignored so existing host JS keeps working. | **FIXED** |

| **Custom load balancer** | `t/js_com_lb_select.t` | `upstream.onSelectPeer(fn)` takes fn's return as a peer index via `JS_ToInt32`, which answers **0** without an error for `undefined`, `NaN`, `{}` and `"nonsense"`. So a selection function that fell off its end without returning — the easiest mistake in a callback whose only job is to return a value — sent **every request to peer 0**: measured B1,B1,B1,B1,B1,B1 where round-robin gives B1,B2,B3, with nothing logged. Only a real finite in-range number is an index now; anything else takes the documented -1 fallback. Negative results: an out-of-range index, a negative one and a throwing callback already fell back correctly, and a suspected `ngx_js_nlbs` reload leak **did not reproduce over 70 reloads**. | **FIXED** |

| **Callback return values** | `t/js_com_filter_nongenerator.t`, lint `t/tools/callback-return-sweep.py` | The companion sweep to the numeric one, built because that lint provably missed the balancer bug: it looks for an ARGUMENT cast into an unsigned field, and a callback RETURN is a different shape. 65 `JS_Call` sites, 11 consuming the return. Ten were correct — `ngx_js_com_ssl.c` requires `JS_IsBool` so only an explicit `return false` rejects a handshake (fail open), `ngx_js_module.c` requires `JS_IsFunction` or throws (fail closed). Six generator sites read `.next` off whatever came back: a body filter returning a non-generator produced an **empty response** — client gets nothing — logged only as `TypeError: not a function`, naming neither filter nor location. Guarded and named. | **FIXED** |

| **Listener / stream methods** | `t/js_com_listener_args.t` | The last untested COM surface and the one I expected to be worst — structural mutation of live routing, in the file pair where the socket aliasing lived, with a `Track L` routing bug in its history. **No defect found.** 17 hostile arguments through 11 methods, at config phase and at request time: 159 refusals, every one a proper Error, 0 malformed, and the listener still resolves its own server name and reports its own address afterwards. Acceptances are pinned per method — `addServer` and `addVirtualServer` (HTTP and stream) take **none** of the battery; `serverByName` takes the four strings and answers null; the L4 registrars take only a function. The file is a ratchet, and it has teeth: weakening one type check in `addVirtualServer` makes nginx **dump core on startup**, so the guard it pins is load-bearing against a NULL dereference. | **CLEAN** |

| **Include contract** | `t/comcon_include_contract_fuzz.t` | Rated lowest-yield of the fuzz rows, and it held the session's second **fail-open admission** defect. Admission is opt-in on the contract naming `imports`/`identity`/`checkRequest`/`tests`, but "naming imports" was decided by truthiness in JS (`contract.imports || ...`) and by `JS_IsObject(imp_h)` in C — and that C condition governed the WHOLE block, free names *and* the dynamic-code denial *and* checkRequest. So `include(src, {imports: 42})` and `{imports: ''}` compiled a fragment with **no gate at all**: measured, `eval("1+1")` admitted where `{imports: []}` refuses it. A contract that looks stricter than it is, is worse than an absent one. A malformed `imports` now means no names granted — fail closed. **Severity, measured rather than assumed (2026-09-12):** this was a defence-in-depth failure, NOT an escape. On the pre-fix build a fragment admitted through the bypass still could not do anything — `eval` and `Function` threw `ReferenceError: ... is not defined` and the free name `nginx` likewise, because the compartment lockdown and the absence of ambient globals are separate mechanisms that held. The commit message for the fix says the fragment compiled "with no gate at all", which is true of the GATE and overstates the consequence; this row is the correction. `imports: undefined` and a contract with no admission fields remain ungated **by design**, pinned so the opt-in boundary stays deliberate. | **FIXED** |

| **No interrupt handler before a worker exists** | `t/comcon_deadline_without_worker.t` | comcon_rt's interrupt handler required a worker (`w != NULL`) before it was installed at all, because it read `w->request_deadline_ms`. A worker does not exist at CONFIG PHASE (`js_source` evaluation, including `nginx -t`), so a fragment's own top-level evaluation, a confined invocation, and an admission test that calls its fragment were ALL genuinely unbounded there — measured, each **hanging until killed**. Fixed: a deadline belonging to the COMPARTMENT (`jcf->comcon_deadline_ms`), not the worker — `jcf` is stable across `fork()`, so the handler installs ONCE at compartment creation, no post-fork re-wiring needed. **A push around the wrong call was found and corrected the same session**: the first attempt wrapped `JS_EvalFunction()`, which only MATERIALIZES the wrapper's closure and never runs it — debug logging showing it return in milliseconds with no exception is what said so. The wrapper's body (where the looping IIFE actually loops) runs at the later `JS_Call()` instead, which is where the fix landed. | **FIXED** |
| **F16 — compiled code could catch its own deadline** | `t/comcon_jit_uncatchable.t` (both builds) | The engine throws the interrupt uncatchable and the interpreter's exception path honours the flag; maxim's generated catch dispatch never asked, so a compiled `try { for(;;){} } catch(e){}` swallowed the interrupt and ran on (measured: "SURVIVED the interrupt" on objs_jit, stopped on objs), and the hostile form loops forever. Found by the authoring tier's basic test, whose PARENT caught its sub-fragment's abort on the JIT build only. Fixed in the engine (`JS_IsUncatchableException()`, one guard in the `_ex:` dispatch, `JIT_CODEGEN_VERSION` 17). **Control by hand** (the fix is in `quickjs/`, outside what `verify-negative-controls.sh` reverts): drop the guard, rebuild the lib and objs_jit, and the test reports SURVIVED. | **FIXED** |
| **F17 — the compartment never freed at worker exit; COM node classes finalizer-less in the compartment** | `t/run_sanitizers.sh` (the corpus, now `detect_leaks=1`; `t/tools/lsan.supp`) | Found by running one test with leak detection on. (a) `ngx_js_exit_process` never tore down `comcon_rt`; one `ngx_js_comcon_teardown()` now serves reload, master exit and worker exit. (b) `ngx_js_http_register_classes`/`ngx_js_upstream_register_classes` were called only by the host's `ngx_js_com_init`, so in the compartment runtime those class IDs had prototypes and no definition — `serverByName()` under audit minted finalizer-less wrappers, leaking opaque + 4 KB pool per call. Both moved into `ngx_js_com_register_classes`. **Control by hand:** revert either and the corpus run reports the `src/js` frame (`ngx_js_comcon_publish` in every comcon file; `ngx_js_wrap_server` under `comcon_v12_denial_codes.t`); both were seen firing before the fixes landed. The corpus ran leaks-off for the SW manager thread's parked 16 bytes, which is now the one suppression. | **FIXED** |
| **The authoring tier's controls (v5.105–v5.107)** | `t/comcon_author_basic.t`, `t/comcon_author_regrant.t`, `t/comcon_author_depth2_gate.t` | Not fixes but claims with controls: the depth-2 battery carries its own (the host arm must be open); both-arms-agree carries a control arm made to differ; the stale-parent chain is the copy-vs-rewrap control (by hand: re-wrap the handle in `ngx_js_socket_narrow()` and a re-grant from the stale parent comes out fresh); the nested bounds (`/nestdeadline`, `/nestmemory`) are the control for phase 1's `min()` (by hand: drop the two min() lines); the text-only crossing is the control for the marshal (by hand: return the object). One row is automated: `37b3c2057` (phase 3) — revert its `src/js` half and `t/comcon_author_regrant.t` fails. | **PINNED** |

| **Wrapper breakout defeats admission entirely** | `t/comcon_wrapper_breakout.t` | `comcon.include()` compiles a fragment by string-concatenating it into `(function(g0,...){"use strict";return(SRC)})` and compiling the WHOLE buffer. Admission only ever inspected the RESULT of that compile, never the rest of the script that produced it — so a source that closed the wrapper early (an unbalanced `)}` inside a string/comment/template literal) and supplied more script-level code ran that code with **NO ADMISSION GATE AT ALL**, even under `imports: []`, the strictest an operator can write. Fixed: compile with `JS_EVAL_FLAG_COMPILE_ONLY` (nothing runs yet), then a new engine helper verifies the compiled unit's bytecode is EXACTLY "create one closure, return it" (checked against the compiler's own opcode output, not a second parser) before `JS_EvalFunction()` ever runs it. **A first version (count nested closures; must be exactly one) was found incomplete before shipping**: sufficient for the fragment wrapper (itself function-shaped, so breaking out always needs a replacement closure), insufficient for `contract.tests`'s bare-paren wrapper, where a comma expression can smuggle in a side effect with no second closure at all — disabling the opcode check reproduced exactly that one gap. Applied to `tests` as a REFUSAL, not a silent skip. Two controls: the whole mechanism reverted (5/13 fail), the opcode check alone disabled (exactly 1/13 fails). | **FIXED** |

| **Shared global bindings, reassignable across fragments** | `t/comcon_global_binding_freeze.t` | Measuring an unrelated, smaller F15 finding turned up something worse: an ORDINARY, PROPERLY ADMITTED fragment body (`imports:['Promise']`) could do `Promise = evil`, and every other fragment reading `Promise` afterwards got the attacker's function -- `imports` gates whether a name may be REFERENCED, never whether the reference is a read or a write. The same reached UN-ADMITTED fragments too (`{}` skips admission by design), where ANY intrinsic could be overwritten with zero gating. **A claim signed 2026-09-12 (G7.6) was INCOMPLETE, not false**: its eight-surface battery tested only VALUE mutation, never BINDING reassignment. Fixed by freezing every binding on the compartment's globalThis once, at creation, independent of admission; globalThis stays extensible so dependency loading (measured: 5 repeated loads, unaffected) keeps working. | **FIXED** |

| **Invocation cost vs a peer's heap** (performance noninterference) | `t/comcon_invoke_heap_independence.t`, `t/comcon_fragment_error_report.t` | Found by reading the invoke path while writing a proposal, and measured before it was believed: every confined invocation called `JS_ComputeMemoryUsage()` twice to read one counter, walking the whole SHARED compartment heap. One worker, one trivial confined call per request: **22.0% of stock idle and 0.2% (476 req/s) while another fragment held 200,000 objects** — a channel set by memory a peer merely holds, beyond any deadline. Now 68.0% / 66.6% via O(1) `JS_GetMallocSize`, gated as a ratio (1.01×; control 173.57×). Chasing a probe's `undefined` found two reporting defects — out of memory read as `null`, and `include()` returning a compartment exception to the host with no value — and **F15**, recorded OPEN: the top-level expression is evaluated before admission, outside the compartment scope, unmetered (it hung `nginx -t`). | **FIXED** (F15 open) |

| **Leftover job accounting** | `t/comcon_leftover_accounting.t` | The last named residual of G6.16, and not the filler it was filed as. The job queue is **FIFO** (`list_add_tail` / `job_list.next`), so a previous fragment's leftovers run FIRST: measured, with 12,000 queued ahead of it a fragment's **entire 10,000-job allowance goes on a stranger's work and its own continuations never run at all** — the original deferred-job escape with the arrow reversed, one fragment SPENDING the next invocation rather than reaching into it. They also ran on that fragment's deadline, and its per-invocation rejection counter reported them as its own. **And measuring it found what the backlog had not named: a leftover's AUTHORITY depended on who arrived next** — `cap.owner` compares against the fragment NOW RUNNING, so the same leftover was denied when a different fragment was drained into and ALLOWED when its own fragment happened to be invoked again, with `ttl`/`window` evaluated at that later moment. Authority decided by traffic order. Leftovers are now drained at the START of an invocation, inside the compartment, under a job budget and a 50 ms deadline of their own and nobody's identity. **The instrument was written from a model twice and was wrong twice** (one invocation leaves ~9,416 behind, three leave 9,000 — both under the next fragment's 10,000 budget, so the control did not fire; it takes FOUR). | **FIXED** |

| **The L4 filter window** (pre-http lifecycle) | `t/js_pilgrim_p17_l4_window.t`, plus the leak stage in `t/run_sanitizers.sh` | Opened by hunting a ~2.8% flake (7/250) and the flake was the least serious of three findings. Between `ngx_event_accept()` and `ngx_http_init_connection()` a connection with an L4 filter armed belongs to `src/js`, and it belonged with **no deadline of any kind**: a client that connected and sent **zero bytes** held a connection slot and its pool **until reload** (measured against the control: dropped at 1 s with no filter, still open at 5 s with one). The same missing timer was the flake — stock nginx never trips the shutdown alert for a connection awaiting its first request because that connection holds a **non-cancelable** `client_header_timeout`, and `ngx_event_no_timers_left()` makes the worker decline to exit while one exists; a timerless window held nothing, so a FIN in flight lost the race to SIGQUIT. Third finding, from the leak instrument written for the first: **13 call sites** in the two pre-http windows closed with `ngx_close_connection()`, which does not destroy `c->pool` — 199 rejects leaked 101,888 bytes. **The instrument was wrong twice** (an RSS probe that moved 0 KB over 3000 rejects, then an ASAN grep that summed nothing because `in N object(s)` is the block header four frames above the `ngx_event_accept` frame), and both times the only reason it was caught is that a control failed to fire. 0/250 after the fix. | **FIXED** |

| **ClientHello parser** (remote input) | `t/js_com_ssl_client_hello.t`, `t/lib/ClientHello.pm` | The only place in `src/js` that parses bytes from an **unauthenticated remote peer** — everything else fuzzed here takes its input from the operator's own host JS — and it was at 0% coverage. 37 hand-built ClientHello records whose server_name and ALPN extensions have valid outer framing and **lying inner lengths** (no TLS library emits these, hence the hand-built records). **No defect found**, and the instrument is validated: all 37 reached the parser (OpenSSL did not filter them, which would have made the file vacuous), and removing the SNI bound check makes ASAN report a SEGV in `ngx_js_build_client_hello` at the exact line — so the corpus reaches the arithmetic, ASAN sees that memory, and the clean result means the bound is doing real work. | **CLEAN** |

**The two halves had opposite coverage**, which is why this survived: the READER
of the name (`tenantLearning()`) sits at 85.7% and is exercised by
`t/comcon_include_learn.t`, while the WRITER was never called by anything. *A
function whose output is consumed by a well-tested function is not thereby
tested.*

**Coverage is not the instrument for the last row**, and that is worth recording
because it was the instrument for the two before it: `ngx_js_rr_peer_set` was
**96% covered** and carried the defect — tests ran that code constantly and never
passed it a negative. Coverage finds unexecuted code; that class hid in
well-executed code and needed a mechanical sweep. See `t/README-coverage.md`.

## 3. GAPS — claims without evidence

An audit that lists only passes is marketing. **Open holes only** — what has been
closed is in §2b, so this table stays short enough to actually check, which is
what §5 asks of a signer.

| Gap | Why it matters | Status |
|---|---|---|
| **Per-fragment memory attribution** | `JS_SetMemoryLimit(comcon_rt, 64 MB)` bounds the *runtime*, shared by every fragment. One fragment can exhaust the budget of all of them — denial of service against siblings, not an authority escape. | **PARTLY CLOSED 2026-09-13** (`t/comcon_fragment_memory.t`, 6): a per-INVOCATION allowance (16 MB default; `contract.meter.memoryBytes` narrows only) enforced by narrowing the runtime limit for the duration of one call and restoring it after — the invoke is single-threaded, so growth in that window is attributable to that fragment. Refusal is `InternalError: out of memory` and the compartment survives it. **Residual:** it bounds a BURST, not a leak — a fragment retaining memory across calls still walks the shared cap up, and the runtime limit remains the only backstop for that. |
| **Cross-compartment identity** | Named in the S6 probe classes; not probed. Low expected yield — the include path marshals via JSON, so only strings cross — but "cannot by construction" is an argument, not a test. | **PROBED 2026-09-12** (`t/comcon_cross_identity.t`, 13). The claim split in two on contact: **host↔fragment is structural** (the compartment is its own `JS_NewRuntime()`, so no value can cross — patching the invoke to pass by reference does not leak, it kills the worker), while **fragment↔fragment shares one runtime AND one context**, separated only by the M-SES-1 freeze, closure-bound grants and admission. Eight surfaces probed as channels, none open; **removing the freeze opens five of seven**, so the mechanism is now identified by removal rather than credited by argument. **Residual found:** an operator who DECLARES `Symbol` for two tenants hands them `Symbol.for` as a rendezvous, and nothing warns them. **[ERRATUM — see §6, 2026-09-13: this residual is WITHDRAWN. The probe that found it compared a value with itself. The attested text above is left as signed; it is wrong.]** |
| **Guarded and irreversible COM members** | The setter fuzz deliberately skips them: `guarded` (8 members) rewires live dispatch and `irreversible` cannot be undone for the process lifetime, so fuzzing either degrades the server under test rather than measuring it. | **OPEN — deliberate scope choice** |
| **Compiled tier under the escape probes** | `t/comcon_mses_gate.t` has been run on both `objs` and `objs_jit`, but AOT-compiled *fragments* (C5 server-AOT) are not separately asserted against the probe battery. SR-2 covers faithfulness of the compiled tier for the confinement surface. | **CLOSED 2026-09-12** (`t/comcon_mses_gate_aot.t`, 9). Running the gate on a JIT-capable BINARY was never the same claim as running it against COMPILED CODE — and `js_comcon_aot_compile()` returns 0 for any bytecode function, so a gate that never checks reports a green compiled tier while measuring an interpreted one. The precondition is now asserted first: **compiled arm 20 natively-lowered functions, interpreted arm 0**, same fragment, same battery. Nothing open on either tier; the two agree probe by probe. Controls: both arms on a non-compiling binary (the precondition refuses), and the freeze disabled on the compiled build only (probes open **on native code**). The battery lives in `t/tools/mses-probes.js`, shared with the standing gate so the two cannot drift. |
| **`nginx.workerRequestTimeout` default** | The per-request deadline for *host JS* remains opt-in (default 0). A runaway `location.handler` — host JS, not a fragment — still hangs the worker. Fragments are bounded; host JS is not. | **CLOSED 2026-09-13** (`t/js_host_request_deadline.t`, 7): the deadline now defaults to **10 s** — not the tenant's 1 s, because host JS is trusted and may legitimately spend real synchronous time in a request. `0` is an explicit opt-out and a malformed value reads as the default (fail closed). The scope choice was reversed because "off by default" protects only the operators who already knew they needed it, while the cost of the old default was a worker hung until SIGKILL. **Residual, now named as F12 in ASSURANCE.md:** the deadline bounds one SYNCHRONOUS entry and is cleared when a handler suspends, so a continuation re-entered from an event callback is still unbounded. |

## 4. Re-running the whole thing

### REBUILD FIRST. The `objs*/` binaries in the tree are stale artifacts.

`objs_jit/` and friends are **tracked in git**, so a fresh clone or a
`git reset --hard` gives you a committed binary, not one built from the source
you are auditing. Re-running §1 against it tests whatever was committed last.

This is not hypothetical: assembling this revision, the escape gate came back
**FAIL** on `objs_jit`, and the cause was a stale committed binary — the real
build passes. An auditor who took that at face value would have recorded a
failing gate; one who took the reverse case at face value would have signed off
on a pass that was never measured. Rebuild every builddir you intend to test,
and check that the binary contains something you know is new:

```bash
for d in objs objs_jit objs_asan objs_ubsan; do
    [ -d "$d" ] && make -f $d/Makefile build -j8
done
strings objs_jit/nginx | grep -c 'must be between'   # a string from the newest fix
```

### Then

```bash
# escape probes + resource guard (both builds)
TEST_NGINX_BINARY=$PWD/objs_jit/nginx prove t/comcon_mses_gate.t t/comcon_fragment_deadline.t
TEST_NGINX_BINARY=$PWD/objs/nginx     prove t/comcon_mses_gate.t t/comcon_fragment_deadline.t

# the whole COMCON corpus
TEST_NGINX_BINARY=$PWD/objs_jit/nginx prove t/comcon_*.t        # 35 files, 336 tests

# the fuzz and range corpus of §2b
TEST_NGINX_BINARY=$PWD/objs/nginx prove \
    t/comcon_declarative_fuzz.t t/js_com_setter_fuzz.t \
    t/js_com_socket_fuzz.t t/js_com_broadcast_fuzz.t \
    t/js_com_numeric_range.t t/js_com_peer_range.t t/js_com_ssl_range.t
                                                    # 7 files, 127 tests

# the cast-not-checked lint (expect 2 known false positives, no more)
python3 t/tools/numeric-cast-sweep.py

# memory safety (builds its own guards in; refuses a vacuous run)
bash t/run_sanitizers.sh

# everything
TEST_NGINX_BINARY=$PWD/objs/nginx prove t/          # 290 files, ~3780 tests
TEST_NGINX_BINARY=$PWD/objs/nginx prove t_stress/   # 18 files, 90 tests
```

Measured 2026-09-11 on a rebuilt tree: escape gate PASS on both builds, ASAN 0
and UBSAN 0 findings in `src/js`, the §2b corpus green, `t/` and `t_stress/`
green.

## 5. Sign-off

Left blank on purpose. To sign, a reviewer who did **not** author the code or the
tests should re-run §4 **on a rebuilt tree** (read the warning there first),
confirm each row of §1 against the named assertions, spot-check §2b by reverting
one fix and watching its named test fail, and confirm §3 still lists every known
gap.

§2b is the part to be most skeptical about: every one of those findings was
reported by the same party that wrote the test that reports it. The negative
controls are what make them checkable — each says which test fails when the fix
is reverted, and that is a claim a reviewer can falsify in minutes:

```bash
bash t/tools/verify-negative-controls.sh            # all rows
bash t/tools/verify-negative-controls.sh 67bc359e9  # one row
```

It reverts only the `src/js` half of each fix (the tests stay, or there would be
nothing to run), rebuilds, and requires the named test to FAIL without the fix
and PASS with it. It refuses to start on a dirty tree and restores on any exit.
**Eight of the twelve rows verify this way; four need a manual revert** because
later commits rewrote the same lines — the script names them and says why rather
than skipping them quietly.

Running that script is not a substitute for reading the code. It checks that the
tests can tell the difference, not that the fixes are the right ones.

### §4 re-run of 2026-09-12 — the evidence this signature points at

Rebuilt all four builddirs first (`objs`, `objs_jit`, `objs_asan`, `objs_ubsan`)
and confirmed the binaries carry the newest fix's string, per the warning above.

| check | result |
|---|---|
| escape gate + fragment deadline, `objs_jit` | 16 tests **PASS** |
| escape gate + fragment deadline, `objs` | 16 tests **PASS** |
| whole COMCON corpus | 36 files / 347 tests **PASS** |
| §2b fuzz + range corpus | 7 files / 127 tests **PASS** |
| `numeric-cast-sweep.py` | exactly **2** hits, both the known false positives (internal-structure reads, not caller input) |
| ASAN | **0** findings in `src/js` |
| UBSAN | **0** in `src/js`; 36 upstream, all `ngx_pstrdup` (`src/core/ngx_string.c:84`) |
| `t/` | 296 files / 3859 tests **PASS** |
| `t_stress/` | 18 files / 90 tests **PASS** |
| `verify-negative-controls.sh` | **8 verified, 0 failed, 0 skipped**; 4 manual rows named with reasons |

§3 re-confirmed complete, by spot-check rather than by reading the text:
cross-compartment identity is still unprobed (`Symbol.species` is a different
probe class, not object identity across the boundary); `workerRequestTimeout`
still defaults to 0; the fuzz-corpus row is correctly gone, closed by the
2026-09-12 run.

| Role | Name | Date | Scope reviewed |
|---|---|---|---|
| Engineering | Vadim Zhestikov | 2026-09-12 | §1–§4 on a rebuilt tree, all green (table above); §3's five gap rows re-confirmed still complete; negative controls 8/8. **The §4 commands were executed by the authoring session, not independently re-run by the signer** — so this attests acceptance of reproducible evidence, not an independent reproduction. Every command is in §4 and re-runnable. |
| Security | Vadim Zhestikov | 2026-09-12 | The five M-SES gate conditions (§1) are evidenced as claimed, incl. the self-validating control (12/12 closed confined, 12/12 open unconfined). §2b findings and their severities accepted, with the negative controls as the check (8/8 automated). **§3's five gaps are ACCEPTED AS RESIDUAL RISK, not closed** — per-fragment memory attribution, cross-compartment identity, `guarded`/`irreversible` COM members, the compiled tier under the escape probes, and host JS still unbounded by default. |

> **BOTH ROWS CARRY THE SAME SIGNER.** The table has two rows so two perspectives
> can attest independently; with one name in both, this is **one attestation, not
> two**, and no second pair of eyes has been applied. Read it that way. A reader
> who needs genuine separation-of-duties should treat the security row as
> outstanding and re-run §4 themselves — every command is there, and
> `verify-negative-controls.sh` makes §2b falsifiable in minutes.
>
> A security signature is an **acceptance of residual risk**, not a statement
> that the gaps are closed. §3 lists exactly what is being accepted.

**Any signature on this table without §3 having been checked is worth less than
no signature**, because it converts "we know these holes exist" into "someone
looked and found nothing".

---

## 6. Changes to the audited surface AFTER the signature (not covered by it)

The §5 signatures are dated 2026-09-12 and attest the tree as of §4's re-run.
This section records changes to the **audited surface** made after that point, so
no reader mistakes the signature for coverage of them. It adds information; it
alters no attested claim, and nothing here is re-signed.

| date | change | why it is not a §3 gap, and what a re-reviewer should check |
|---|---|---|
| 2026-09-13 | **The `Symbol.for` RESIDUAL IS WITHDRAWN — it was an artefact of a dead probe.** §3's row reports that declaring `Symbol` for two tenants "hands them `Symbol.for` as a rendezvous". The arm that measured it read `Symbol.for(k) === Symbol.for(k) ? 'SHARED' : 'clean'` — **two calls in the same fragment, compared with each other**, which is true of any registry, private or shared, and never looked at another fragment; its plant returned a `typeof` that nothing consumed. A probe whose read cannot be false is not a probe. Rewritten (`t/comcon_cross_identity.t`) to attempt the whole exploit: take the symbol and use it as a property KEY on every surface both fragments touch. **Result: `proto:refused,json:refused,array:refused` and the read is `clean`.** A shared registry gives two fragments the same key; **a key is not a channel without a STORE to unlock**, and every store they share is frozen. | **It RETRACTS a residual rather than adding a change.** Two controls make it a measurement: the same text UNCONFINED reads the mark back — which proves the key matched across two separate calls, so the registry really is runtime-wide — and with the M-SES-1 freeze disabled the CONFINED arm becomes a live channel (4 assertions fail). So the mechanism is named by removal: the freeze, not the absence of a shared name. A re-reviewer should check that the confined read is `clean` **and** that the unconfined arm is `planted`; a clean confined result with a dead control means nothing. **A per-fragment `Symbol` facet was built and then REJECTED** — it would have broken erasure (ASSURANCE G11.7): two fragments of one program calling `Symbol.for('k')` would get different symbols under COMCON than in plain node, which is an annotation changing what code computes. |
| 2026-09-12 | **C3 admission gained an INTRINSICS category** (`ngx_js_admit_intrinsics[]`, `src/js/ngx_js_com.c`): a short list of language values — `undefined`/`Object`/`Array`/`String`/`Number`/`Boolean`/`BigInt`/`JSON`/`RegExp`/`Map`/`Set`/`WeakMap`/`WeakSet`/the `Error` types/`parseInt`/`parseFloat`/`isNaN`/`isFinite`/the URI helpers — is admitted **without appearing in `imports`**. Decided by the user after the V3 kernel oracle found the gate refusing an ordinary `x !== undefined`. | It **widens a declaration requirement, not a reach**: every name on the list was already reachable at runtime inside the compartment (the fragment ran with the standard intrinsics either way, and a fragment with admission OFF used them freely) — the gate previously demanded they be *named* in a manifest that grants nothing for them. Row **(a)** of §1 rests on "no ambient **host** globals in the compartment", which is unchanged: `nginx`, the COM root, `globalThis` and the rest are still refused, and the `t/comcon_mses_gate.t` probes that back row (a) are untouched. **Deliberately excluded and still requiring declaration: `Date` and `Math`** (clock and RNG — the side channels §3 leaves open), plus `Promise`, `Symbol`, `Proxy`/`Reflect` and the `ArrayBuffer` family. A re-reviewer should check `t/comcon_v3_oracle.t` (the three categories asserted one by one) and `t/tools/check-enumerations.py` check 4, which fails the suite if `Date`/`Math` are ever added to the list or if the engine's list and the V3 model's copy drift apart. |
| 2026-09-12 | **`contract.intrinsics` — the allowance narrows per contract.** The list above is now a default, not a floor: `{intrinsics: []}` refuses every free name including language values, `{intrinsics: ['JSON']}` permits exactly that one. | Added because the row above **removed an expressible policy**: before the allowance, `{imports: []}` meant "no free names at all", and afterwards nothing spelled that. This restores it and goes no further — `intrinsics` can only NARROW (naming a non-intrinsic is refused, pointing at `imports`), it switches admission on by itself so it cannot be inert, it survives `realize()`, and present-but-malformed reads as the strictest setting. A re-reviewer wanting the pre-allowance strictness should look for `intrinsics: []`, or `std.profiles.pure_library({intrinsics: []})`. |
