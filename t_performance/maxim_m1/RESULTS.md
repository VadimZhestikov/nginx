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

## Method + result — two setups

Both use the same three binaries/configs; they differ only in how load is applied.

### (a) Loopback (n6, same-host generator)

`h2load -t4 -c100 --h1 -m1 -n 300000 -H 'x-tenant: acme'`, best of 3.

| Config | req/s | % of stock floor | × vs interpreted |
|---|--:|--:|--:|
| stock nginx (no policy) | 391,233 | 100% | — |
| **maxim hand-C policy** | **350,165** | **90%** | **3.09×** |
| interpreted mirror policy | 113,327 | 29% | 1.0× |

### (b) Table A′ method — real 10 GbE, cross-host

n6 DUT served over `eno4` (10.98.4.1) to a *separate* generator host n8 running
`h2load -t8 -c100 --h1 -m1 -n 400000 -H 'x-tenant: acme'`, best of 3. No same-host
core-sharing → cleaner absolutes.

| Config | req/s | % of stock floor | × vs interpreted |
|---|--:|--:|--:|
| stock nginx (no policy) | 424,882 | 100% | — |
| **maxim hand-C policy** | **409,386** | **96%** | **3.44×** |
| interpreted mirror policy | 119,044 | 28% | 1.0× |

## Verdict — GATE PASSED

The C that a typed COMCON policy would compile to runs at **90–96% of bare nginx** and
**3.1–3.4× the interpreted mirror** — and the cleaner cross-host setup (b) gives the
*stronger* result (96% / 3.44×), so the conclusion is not a loopback artifact. A compiler
that lowers typed policy JS to this shape reclaims essentially all of the ~3×
interpretation gap → **the compiler is worth building** (proceed to M2).

## Honest caveats

- **Loopback (a) has ~10–15% run-to-run noise** (same-host generator shares cores): the
  stock floor measured 326k then 391k across two runs. This is exactly why setup (b) was
  added — the cross-host real-NIC run tightens the absolutes (stock 425k, maxim 409k
  within one run) and *strengthens* the ratio (3.44×). The conclusion holds under both.
- **This is the ceiling, not the compiler's output.** Hand-C is maximally direct; real
  maxim output may retain some framework structure and land between the interpreted
  (~119k) and hand-C (~409k) numbers, closer to hand-C the more direct the typed lowering.

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
#
# (a) loopback: run h2load on n6 against http://127.0.0.1:8080/
# (b) Table A' cross-host: from n8, h2load against http://10.98.4.1:8080/ (n6 eno4).
#     NB: wrap ssh in the runner with `ssh -n` (or </dev/null) — an ssh call inside a
#     heredoc otherwise slurps the rest of the script from stdin and the loop stops
#     after the first iteration.
```

## Reduced policy — adding the `nginx directives/vars` baseline

The full "count + tag" policy above is **not** expressible in stock nginx directives —
the per-request global atomic counter (`ev.table.incr`) has no directive/variable
equivalent (`$connection` counts connections, not requests; `$request_id` is random;
`limit_*` expose no count). To get a fair fourth data point, the policy was **reduced to
its directive-expressible subset** — the "tag" only: echo `x-tenant` → `x-tenant-seen`
(default `-`) + `ok` body. This drops exactly the shared-state part and keeps the
stateless request→response mapping.

All four layers implement this identical reduced policy:

- **nginx directives/vars** (`directives.conf`, stock binary): `map $http_x_tenant …` +
  `add_header x-tenant-seen $tenant_seen always;` + `return 200 "ok\n";`
- **maxim hand-C** (`ngx_http_maxim_tag_module.c`): content handler, header read + one
  response header + body; no shm.
- **interpreted mirror** (`maxim_mirror_tag_app.js`): `onRequestHeaders` stashes
  `ev.header('x-tenant')` in `ev.flow`, `onResponseHeaders` emits it.
- **stock floor**: `return 200 "ok\n"`, no policy.

Same method as above (h1 keepalive `-c100`, best of 3).

| Config | loopback (a) | % | cross-host (b) | % |
|---|--:|--:|--:|--:|
| stock floor (no policy) | 356,681 | 100% | 402,800 | 100% |
| **nginx directives/vars** | **368,691** | **103%** | **376,639** | **94%** |
| maxim hand-C (tag) | 358,677 | 101% | 388,052 | 96% |
| interpreted mirror (tag) | 168,704 | 47% | 175,838 | 44% |

**Finding.** For a policy simple enough to express declaratively, **directives ≈ hand-C ≈
stock floor** — all within run-to-run noise (94–103%); the `map`+`add_header` path is
essentially free. The interpreted mirror still pays ~2.2× (44–47% of stock). So on the
reduced policy the entire gap *is* the interpreter, and directives already sit at the
compiled-C ceiling.

**Why this sharpens the thesis, not softens it.** Declarative config is the right (and
free) tool for the stateless part — so the value of maxim is precisely the policies
directives **cannot** express: the atomic counter of the full policy, rate-limit token
buckets, dynamic routing tables, cross-request shared state. There, the only stock
option is the interpreted layer (the 44% row), and maxim's job is to pull that back up to
the ~96% hand-C ceiling. The reduced table is the control that isolates where compilation
actually pays: not the declarative parts (directives already win), but the stateful/logic
parts (where directives drop out entirely).

## Spike gotchas (worth remembering)

1. **`return 200` finalizes in the REWRITE phase** — before ACCESS — so an ACCESS-phase
   handler never runs. Do policy work in a **content handler** (parallels mirror's
   `loc.handler`) or a header filter, not an access handler sitting behind `return`.
2. **An addon added via plain `HTTP_MODULES=`** lands in `ngx_modules[]` *before*
   `ngx_http_header_filter_module`, so a header-filter addon runs *after* the headers
   are already serialized (too late). Use a content handler, or register the addon as
   `HTTP_FILTER` type.
3. **`ssh -n` vs the heredoc.** `ssh -n` redirects ssh's *own* stdin from `/dev/null` —
   use it only for ssh calls nested *inside* a heredoc-driven script (else the inner ssh
   slurps the rest of the script and the loop dies after one iteration). Do **not** put
   `-n` on the outer `ssh 'bash -s' <<EOF` call itself — it would swallow the heredoc and
   the remote shell gets empty input (runs nothing).
