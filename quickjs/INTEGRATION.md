# pilgrim-quickjs — the single integration fork

This repo is the **single clean lineage** for the QuickJS engine that the
nginx *pilgrim* tree vendors under `quickjs/` (COMCON's compiled tenant tier).
It exists so that bellard (upstream), maxim (the JIT/AOT compiler), and
pilgrim's own COMCON/host patches merge **here, once**, and pilgrim only ever
`git subtree pull`s an already-reconciled result — instead of the two trees
being hand-synced (cherry-picked) back and forth, which is how latent crash
fixes previously went missing in one tree. (See pilgrim `CLAUDE.md` →
"Updating the vendored engine".)

## Remotes

| remote    | repo                                   | role                          |
|-----------|----------------------------------------|-------------------------------|
| `bellard` | github.com/bellard/quickjs             | upstream engine (CVEs, bases) |
| `maxim`   | github.com/VadimZhestikov/quickjs      | the JIT/AOT compiler (`jit`)  |

## Branches

- **`jit`** — tracks `maxim/jit`: bellard 2025-09-13 + 186 JIT commits
  (warm-IC P47–P54, generator GC marking, the bucket-1 / cache-load /
  back-edge-gas crash+gas fixes, the T0 test262-delta harness). This is the
  engine baseline; never commit pilgrim-specific code here.
- **`pilgrim`** — the integration branch = `jit` + pilgrim's engine delta.
  **This is the branch pilgrim subtree-pulls from.** HEAD `97063cd`.

## The pilgrim delta (what `pilgrim` adds over `jit`)

All additive host/COMCON code the nginx module needs; no engine-semantics
changes beyond one bellard-snapshot reconcile:

| file(s)                | addition                                                        |
|------------------------|----------------------------------------------------------------|
| `quickjs.h`/`quickjs.c`| COMCON admission+lowering API: `js_comcon_collect_free_globals`, `js_comcon_uses_dynamic_code`, `js_comcon_check_request_fields`, `js_comcon_aot_compile` (all static — no fragment code runs) |
| `quickjs-jit.c`        | `jit_atfork_child` + `pthread_atfork` (C1.3): a forking host must not inherit a phantom GCC-worker thread + locked queue mutex |
| `quickjs-libc.c/.h`    | `js_std_add_timer_globals`, `js_std_tick_timers`: non-blocking setTimeout/timer glue for the nginx event loop |

**Bellard-snapshot reconcile point:** pilgrim carries a slightly newer bellard
JSON snapshot (`rawJSON` atom + JSON.parse superset) than maxim's older
`JS_ParseJSON2` form — both tag `VERSION 2025-09-13`. Resolved on pilgrim's
(newer) side, since that is the tested, shipping engine. This is the one place
a future `git merge maxim/jit` may re-touch; take pilgrim's side unless maxim
rebases onto the newer bellard.

By construction the `pilgrim` tree's **engine source is byte-identical to
pilgrim's currently-vendored `quickjs/`** — so the first cutover is an engine
no-op (it only adds maxim's T0 harness + standard repo-infra files).

## Workflows

### Pull a new maxim JIT advance into pilgrim
```
cd pilgrim-quickjs
git fetch maxim && git checkout jit && git merge --ff-only maxim/jit
git checkout pilgrim && git merge jit        # resolve JSON reconcile toward pilgrim if it surfaces
make CONFIG_JIT=y libquickjs.a qjs && make CONFIG_JIT=y test   # engine self-gate
# then, in the pilgrim nginx tree:
git subtree pull --prefix=quickjs <this-repo> pilgrim --squash
# GATE before committing the pull: rebuild both nginx variants, run
#   TEST_NGINX_BINARY=.../objs/nginx     prove t/comcon_*.t   (interpreter)
#   TEST_NGINX_BINARY=.../objs_jit/nginx prove t/comcon_*.t   (JIT)
#   + t/comcon_faithfulness.t (SR-2) + full t/ + t_stress/    (see pilgrim build-and-test memory)
```

### Pull a bellard CVE / base bump
```
git fetch bellard && git checkout jit && git merge maxim/jit   # let maxim absorb bellard first if possible
# else 3-way it here on jit, then merge jit -> pilgrim as above.
```

### First cutover (Phase 2 — gated, not yet done)
Engine no-op; adds T0 + repo-infra to pilgrim's `quickjs/`. Still run the full
SR-2 + regression gate above so the subtree-tracking switch is proven, then it
becomes the standing workflow.
