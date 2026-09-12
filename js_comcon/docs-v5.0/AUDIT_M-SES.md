# M-SES audit checklist (S6)

**Status: UNSIGNED.** Evidence assembled 2026-09-11 and re-measured 2026-09-12
after fourteen commits of hardening (§2b); the sign-off block at the end is
deliberately blank. An audit certifies a date — if you are reading this well
after the one above, re-run §4 before trusting any row. An audit attested by the party that wrote the code
and the tests certifies nothing — a human who did not write them signs, or it
stays unsigned and is read as "evidence assembled", which is all it currently
is.

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
| **Per-fragment memory attribution** | `JS_SetMemoryLimit(comcon_rt, 64 MB)` bounds the *runtime*, shared by every fragment. One fragment can exhaust the budget of all of them — denial of service against siblings, not an authority escape. | **DEFERRED BY DESIGN** (HARDENING S5). Partially evidenced: `t/js_worker_memory_limit.t` proves the mechanism on the HOST runtime, but exercises `nginx.workerMemoryLimit`, not the 64 MB cap on `comcon_rt`, and nothing asserts a *fragment* hitting it. |
| **Cross-compartment identity** | Named in the S6 probe classes; not probed. Low expected yield — the include path marshals via JSON, so only strings cross — but "cannot by construction" is an argument, not a test. | **NOT EVIDENCED** |
| **Guarded and irreversible COM members** | The setter fuzz deliberately skips them: `guarded` (8 members) rewires live dispatch and `irreversible` cannot be undone for the process lifetime, so fuzzing either degrades the server under test rather than measuring it. | **OPEN — deliberate scope choice** |
| **SSL client-hello path** | ~200 lines at 0% coverage (`ngx_js_ssl_on_client_hello`, `ngx_js_ch_cb`, `ngx_js_build_client_hello`). Reaching it needs a real TLS handshake from a client, not a COM walk. The four scalar `ssl.*` setters beside it ARE now covered (§2b). | **NOT EVIDENCED** |
| **Compiled tier under the escape probes** | `t/comcon_mses_gate.t` has been run on both `objs` and `objs_jit`, but AOT-compiled *fragments* (C5 server-AOT) are not separately asserted against the probe battery. SR-2 covers faithfulness of the compiled tier for the confinement surface. | **PARTIAL** |
| **`nginx.workerRequestTimeout` default** | The per-request deadline for *host JS* remains opt-in (default 0). A runaway `location.handler` — host JS, not a fragment — still hangs the worker. Fragments are bounded; host JS is not. | **OPEN — deliberate scope choice** (bind the guard to the confined path only, so host JS behaviour does not change) |

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

| Role | Name | Date | Scope reviewed |
|---|---|---|---|
| Engineering | | | |
| Security | | | |

**Any signature on this table without §3 having been checked is worth less than
no signature**, because it converts "we know these holes exist" into "someone
looked and found nothing".
