# src/js test coverage — how to measure it, and what it said

The one genuine memory-safety defect found while fuzzing this surface (a
misaligned header load in the broadcast receive path, commit `d3a438051`) was
not found by a better generator. It was found because a test finally
**executed** that code under UBSAN. Sixteen clean sanitizer runs had already
passed over it, because nothing reached it.

That makes coverage the cheapest way to pick the next target: a sanitizer only
sanitizes what you run, so the unexecuted functions are where the next defect of
that class is sitting. This file records how to reproduce the measurement and
what the first one found.

## Measuring

```bash
# a fourth builddir, instrumented; -O0 so line attribution is honest
auto/configure --builddir=objs_cov --add-module=src/js \
  --with-cc-opt='-Iquickjs -Wno-cast-function-type --coverage -O0 -g' \
  --with-ld-opt='-Lquickjs -lquickjs -lm --coverage' \
  <the same --with-http_* modules as objs> --with-stream
make -f objs_cov/Makefile build -j8

# counters accumulate across runs, so run BOTH suites before reporting
TEST_NGINX_BINARY=$PWD/objs_cov/nginx prove t/
TEST_NGINX_BINARY=$PWD/objs_cov/nginx prove t_stress/

for c in src/js/*.c; do gcov -f -o objs_cov/addon/js/ "$c"; done
rm -f ./*.gcov          # gcov writes these into the CWD; they are not wanted
```

`objs_cov/` and the stray `*.gcov` are build output — add them to `.gitignore`
locally or clean up as above; neither belongs in a commit.

Report `t/` alone and you will misread it: `t_stress/` is where the peer setter
stress lives, and a function it covers looks dead without it.

## First measurement (2026-09-11, `d3a438051` + this commit)

**75.7% of 22 562 lines; 94 functions (962 lines) never executed.**

Ignore the `JS_FreeValue` / `JS_DupValue` entries — those are static inlines
from `quickjs.h` instantiated per translation unit, and an unused copy means
nothing.

Triaged, highest value first:

| what | lines at 0% | why it matters |
|---|---|---|
| **`NginxPeer` (`ngx_js_peer_get`/`_set`)** | 40 | **Acted on in this commit.** The config-phase twin of the round-robin peer view. Every test reaches peers at *request* time, so this class had never run — and the twins had drifted: neither validated, but this one writes an `ngx_uint_t`, so `weight = -1` stored ~1.8e19 into the load balancer. |
| `ngx_js_com_ssl` client-hello surface | 239 (44.8% file) | `ngx_js_ssl_set`, `ngx_js_ssl_on_client_hello`, `ngx_js_ch_cb`, `ngx_js_build_client_hello`. A whole feature with no test; `ngx_js_ssl_set` is a setter taking untrusted values, missed by `js_com_setter_fuzz.t` only because its config has no `ssl` server. Cheapest next win: add one. |
| `ngx_js_com_upstream` load-balancer registry | ~100 | `ngx_js_lb_choose`, `_get`, `_init`, `_find`, `_registry`, `ngx_js_upstream_on_select_peer` — the custom-LB hook, entirely unexercised. |
| `ngx_js_com_stream_upstream` | 165 (58.8% file) | `ngx_js_stream_rr_peer_set` (47 lines) is the stream twin of the peer setter fixed here, and is still untested. **Likely carries the same defect.** |
| `ngx_js_com_access` | 73 | `ngx_js_access_set_rules6` (IPv6) and `_unix` (unix sockets) — the IPv4 path is tested, these are not. |
| `ngx_js_listener` connection/L4 | ~90 | `ngx_js_connection_on_close`, `ngx_js_conn_state*`, `ngx_js_l4_send`, `ngx_js_connection_get_ctx`. |
| `ngx_js_grant_to_tenant` | 29 | Still present after the tenant path was retired (M-CFG convergence). Check whether it is now dead code to delete rather than test. |

Percentages are a poor target — plenty of the remainder is fork paths and
syscall-failure branches that a test rig cannot reach, and chasing the number
would be worse than useless. The list above is the part worth acting on.
