# M1 — COMCON+maxim perf spike (hand-written C vs interpreted mirror)

**Milestone M1** of the typed-COMCON-policy → maxim-compiled-C plan: prove *cheaply*,
before building any compiler, that the C a typed policy would compile to runs at
~native speed. This is a throwaway spike — the deliverable is the number and the
gate decision, not shippable code.

## What was measured

One "count + tag" policy, implemented two ways doing **identical work**:

| Step | interpreted mirror | hand-written C (`ngx_http_maxim_spike_module.c`) |
|---|---|---|
| shared counter incr | `ev.table.incr('maxim:count')` | `ngx_atomic_fetch_add` on a slab-shm atomic |
| read request header | `ev.header('x-tenant')` | walk `r->headers_in.headers` for `x-tenant` |
| 2 response headers | `ev.setResponseHeader('x-count'…)` ×2 | `ngx_list_push(&r->headers_out.headers)` ×2 |
| body | `r.respond(200,…,'ok\n')` | content handler emits 3-byte `ok\n` |

- **interpreted**: `nginx-pilgrim` + `mirror.js` + `maxim_mirror_app.js` (mirror rule).
- **hand-C**: stock nginx built with `--add-module=maxim-spike`; the policy is a
  content handler (see gotchas).
- **stock floor**: bare `return 200 "ok\n"`, no policy — reference only.

All three on an **identical skeleton**: `listen 8080 reuseport; access_log off;`,
4 workers.

## Method

n6 loopback, `h2load -t4 -c100 --h1 -m1 -n 300000 -H 'x-tenant: acme'`, best of 3.

## Result

| Config | req/s | % of stock floor | × vs interpreted |
|---|--:|--:|--:|
| stock nginx (no policy) | 391,233 | 100% | — |
| **maxim hand-C policy** | **350,165** | **90%** | **3.09×** |
| interpreted mirror policy | 113,327 | 29% | 1.0× |

## Verdict — GATE PASSED

The C that a typed COMCON policy would compile to runs at **~90% of bare nginx** and
**~3.1× the interpreted mirror**. A compiler that lowers typed policy JS to this shape
reclaims essentially all of the ~3× interpretation gap → **the compiler is worth
building** (proceed to M2).

## Honest caveats

- **Loopback + same-host generator ⇒ ~10–15% run-to-run noise.** Across two runs the
  stock floor measured 326k then 391k; maxim 396k then 350k. The **ratios**
  (maxim:interpreted ≈ 3.1–3.2×) are stable; the absolute floor is noisy. A cross-host
  real-NIC repeat (Table A′ method) would tighten the absolutes but not the conclusion.
- **This is the ceiling, not the compiler's output.** Hand-C is maximally direct; real
  maxim output may retain some framework structure and land between 113k and 350k,
  closer to 350k the more direct the typed lowering.

## Reproduce

```bash
# on n6 (js_com kern-src checked out at ~/kern-src):
cp -r maxim_m1 ~/maxim-spike && mv ~/maxim-spike/RESULTS.md /tmp   # module + config only
cd ~/kern-src
auto/configure --with-http_v2_module --with-http_ssl_module --with-stream \
    --add-module=$HOME/maxim-spike
make -j$(nproc) && cp objs/nginx ~/tableA/nginx-maxim
# configs + runner: see the ladder in the session notes (stock_m.conf / maxim.conf /
# mirror_maxim.conf, all "listen 8080 reuseport; access_log off;").
```

## Spike gotchas (worth remembering)

1. **`return 200` finalizes in the REWRITE phase** — before ACCESS — so an ACCESS-phase
   handler never runs. Do policy work in a **content handler** (parallels mirror's
   `loc.handler`) or a header filter, not an access handler sitting behind `return`.
2. **An addon added via plain `HTTP_MODULES=`** lands in `ngx_modules[]` *before*
   `ngx_http_header_filter_module`, so a header-filter addon runs *after* the headers
   are already serialized (too late). Use a content handler, or register the addon as
   `HTTP_FILTER` type.
