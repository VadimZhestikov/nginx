# M-SES audit checklist (S6)

**Status: UNSIGNED.** Evidence assembled 2026-09-11; the sign-off block at the
end is deliberately blank. An audit attested by the party that wrote the code
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

| # | Claim | Mechanism | Evidence | Verdict |
|---|---|---|---|---|
| **a** | A fragment cannot obtain a value outside its ρ | free-name gate at admission + no ambient globals in the compartment | `t/comcon_mses_gate.t` probes `a_global_this`, `a_com_root`, `a_comcon` (closed confined, **open unconfined**); `t/comcon_include_admit.t` refuses an ungranted free name; `t/comcon_include_grant.t` asserts `"nginx":"undefined"` inside the fragment | **EVIDENCED** |
| **b** | A fragment cannot mutate a frozen intrinsic | M-SES lockdown freezes core intrinsics | `t/comcon_mses_gate.t` probes `b_proto_poll`, `b_array_push`, `b_freeze_str` (closed confined, open unconfined) | **EVIDENCED** |
| **c** | A fragment cannot create code from strings without admit | dynamic-code taming (`%Function%`, generator/async constructors, eval) | `t/comcon_mses_gate.t` probes `c_fn_ctor`, `c_obj_ctor`, `c_gen_ctor`, `c_async_ctor`; `t/comcon_include_mses.t` asserts `routes=closed,closed,closed,closed` | **EVIDENCED** |
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
| ASAN | 34 COMCON files, 313 tests | **0 findings** |
| UBSAN | 34 COMCON files, 313 tests | **0 findings in `src/js`**; 34 in stock nginx, all one site (`ngx_pstrdup`, `src/core/ngx_string.c:84` — `memcpy(dst, NULL, 0)` at cycle init; upstream's, benign) |

Re-run: `bash t/run_sanitizers.sh`.

**The suites' own `ok - no sanitizer errors` line is VACUOUS** — neither `objs/`
nor `objs_jit/` is built with a sanitizer, so it has nothing to look at. Only the
script above produces a meaningful answer, and it carries four guards for that
reason: the binary must carry the sanitizer's symbols; a positive control must
land a report through the same `prove` pipeline; the run must have executed
tests; and a file skipping with `no js module` means nginx could not start. The
last two exist because the script produced a vacuous PASS on itself twice while
being written.

## 3. GAPS — claims without evidence

An audit that lists only passes is marketing. These are the holes as of
2026-09-11:

| Gap | Why it matters | Status |
|---|---|---|
| **Per-fragment memory attribution** | `JS_SetMemoryLimit(comcon_rt, 64 MB)` bounds the *runtime*, shared by every fragment. One fragment can therefore exhaust the budget of all of them — a denial-of-service against siblings, not an authority escape. | **DEFERRED BY DESIGN** (HARDENING S5, "coarse runtime limits + gas first"). Partially evidenced: `t/js_worker_memory_limit.t` proves the mechanism works on the HOST runtime — an OOM under a 1 MB cap is caught and the worker survives — but it exercises `nginx.workerMemoryLimit`, not the hardcoded 64 MB cap on `comcon_rt`, and nothing asserts a *fragment* hitting it. |
| **Cross-compartment identity** | Named in the S6 probe classes; not probed. Low expected yield — the include path marshals via JSON, so only strings cross and object identity cannot survive by construction — but "cannot by construction" is an argument, not a test. | **NOT EVIDENCED** |
| **Fuzz corpus** | S6 names one. A probe suite tests the attacks we thought of. | **NOT STARTED** |
| **Compiled tier under the escape probes** | `t/comcon_mses_gate.t` runs on whatever build the suite runs on; it has been run on both `objs` and `objs_jit`, but AOT-compiled *fragments* (C5 server-AOT) are not separately asserted against the probe battery. SR-2 covers faithfulness of the compiled tier for the confinement surface. | **PARTIAL** |
| **`nginx.workerRequestTimeout` default** | The per-request deadline for *host JS* remains opt-in (default 0). A runaway `location.handler` — host JS, not a fragment — still hangs the worker. Fragments are bounded; host JS is not. | **OPEN — deliberate scope choice** (2026-09-11: bind the guard to the confined path only, so host JS behaviour does not change) |

## 4. Re-running the whole thing

```bash
# escape probes + resource guard (both builds)
TEST_NGINX_BINARY=$PWD/objs_jit/nginx prove t/comcon_mses_gate.t t/comcon_fragment_deadline.t
TEST_NGINX_BINARY=$PWD/objs/nginx     prove t/comcon_mses_gate.t t/comcon_fragment_deadline.t

# the whole COMCON corpus
TEST_NGINX_BINARY=$PWD/objs_jit/nginx prove t/comcon_*.t        # 34 files, 313 tests

# memory safety (builds its own guards in; refuses a vacuous run)
bash t/run_sanitizers.sh
```

## 5. Sign-off

Left blank on purpose. To sign, a reviewer who did **not** author the code or the
tests should re-run §4, confirm each row of §1 against the named assertions, and
confirm §3 still lists every known gap.

| Role | Name | Date | Scope reviewed |
|---|---|---|---|
| Engineering | | | |
| Security | | | |

**Any signature on this table without §3 having been checked is worth less than
no signature**, because it converts "we know these holes exist" into "someone
looked and found nothing".
