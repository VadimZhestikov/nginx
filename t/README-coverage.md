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
| `ngx_js_ssl_set` (4 scalar TLS props) | 37 | **Done** — `t/js_com_ssl_range.t`. The SSL tests next to it cover the *methods* (`setCertificate`, `setCiphers`, `setProtocols`) and the getters, so the four scalar setters had never been written to. Same defect as the peers: `verifyDepth = {}` silently set the verification depth to 0. |
| `ngx_js_com_ssl` client-hello surface | ~200 (44.8% file) | **Done** — `t/js_com_ssl_client_hello.t` with `t/lib/ClientHello.pm`, which builds TLS records by hand because a TLS library will not send a malformed extension. No defect; the bound checks hold, verified by removing one and watching ASAN SEGV at that line. |
| `ngx_js_com_upstream` load-balancer registry | ~100 | **Done** — `t/js_com_lb_select.t`. A callback that forgot to `return` sent every request to peer 0, because `JS_ToInt32` reads `undefined` as 0. Found only because the feature had no test at all. |
| `ngx_js_com_stream_upstream` | 165 (58.8% file) | `ngx_js_stream_rr_peer_set` (47 lines) is the stream twin of the peer setter fixed here, and is still untested. **Likely carries the same defect.** |
| `ngx_js_com_access` | 73 | `ngx_js_access_set_rules6` (IPv6) and `_unix` (unix sockets) — the IPv4 path is tested, these are not. |
| `ngx_js_listener` connection/L4 | ~90 | `ngx_js_connection_on_close`, `ngx_js_conn_state*`, `ngx_js_l4_send`, `ngx_js_connection_get_ctx`. |
| `ngx_js_grant_to_tenant` | 29 | **Done** — `t/js_com_grant_declare.t`. Neither dead nor working: it recorded a socket handle nothing read, so it reported a capability it did not confer. Reduced to the name registry it actually is. A reminder that 0% does not mean "delete": half its output fed a function sitting at 85.7%. |

## A class coverage could not have found

The three defects above were one defect -- a JS number CAST into a config field
rather than checked -- and coverage is the wrong instrument for it.
`ngx_js_rr_peer_set` was **96% covered** and had it: tests executed that code
constantly, they just never passed it a negative. Coverage finds unexecuted
code; this hid in well-executed code.

So it was swept mechanically instead: `t/tools/numeric-cast-sweep.py` enumerates
every place a converted value is cast into an unsigned or time field. The first
run found 94 sites, 81 already guarded, 13 not -- of which 4 were false
positives and **9 were real**, in two families (response status, stream peers).
All now go through one shared check, `ngx_js_com_num_range()` in
`ngx_js_com.c`, and the two previously-local helpers were collapsed onto it,
because three copies of a check is how the HTTP and stream peer setters came to
differ in the first place.

There is a second lint, `t/tools/callback-return-sweep.py`, for the shape this
one misses: a value that arrives as a callback RETURN rather than an argument.
It exists because running the numeric lint over the balancer bug (a321849fa)
reports it clean. Callback returns deserve more suspicion than arguments, not
less — they are produced per request by a script that can be silently wrong.

The tool's site count drops as sites are fixed (a converted-then-cast pair
becomes a single `ngx_js_com_num_range()` call, which it no longer counts). It
is a lint, not an oracle: judge each hit, since a length read off an internal
array is not caller input.

Percentages are a poor target — plenty of the remainder is fork paths and
syscall-failure branches that a test rig cannot reach, and chasing the number
would be worse than useless. The list above is the part worth acting on.
