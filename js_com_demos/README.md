# js_com_demos

64 self-contained, runnable demos organised by value category. Each demo has
its own `nginx.conf`, `handler.js` (or `gen.js`), `test.sh`, and `README.md`.

> See [`../js_com_docs/feature-request-coverage.md`](../js_com_docs/feature-request-coverage.md)
> for how these demos map to real (anonymized) customer feature requests that
> current NGINX does not cover.

## Quick start

```bash
# One demo
cd A1_Live_Infrastructure_Surgery/A1.5_Feature_flag_a_location
bash test.sh

# All demos (stop on first failure)
for t in $(find . -name test.sh | sort); do
    echo "=== $t ==="; bash "$t" || break
done
```

The nginx binary is assumed at `../../objs/nginx` (relative to each group
directory, resolving to `objs/nginx` in the repo root).

---

## Top 5 showstopper demos (for a talk or live session)

| # | Demo | What the audience sees |
|---|------|------------------------|
| 1 | **A1.1** Live SSL cert rotation | HTTPS traffic continues while the cert is swapped live — no reload, no drop |
| 2 | **A1.2** Add virtual host at runtime | POST one request → new vhost appears; curl with `Host:` header proves it works |
| 3 | **C1.2** Parallel fan-out + **B3.3** Fan-in | 3 internal subrequests → merged JSON response; README shows wrk comparison vs SSI |
| 4 | **A3.1** 200 vhosts from one JSON file | `js_preprocess` generates 200 server{} blocks in < 100 ms; `nginx -T` shows them all |
| 5 | **B2.1** Inline JWT + **C1.1** JWT cache | JWT decoded in-process; SAB cache means 2nd call hits < 1 µs — benchmark live |

---

## Demo index

### A — Only Possible With This Project

#### A0 — COM API Extension (port 8099)

| Demo | Port | What it shows |
|------|------|---------------|
| [A0.1 Custom nginx property](A0_COM_API_Extension/A0.1_Custom_nginx_property/) | 8099 | `nginx.use()` adds `nginx.featureFlags` to the COM object — the pattern every other demo uses |

> **Start here.** Every demo in this collection extends the nginx COM object with
> a user-defined JS API.  A0.1 isolates that pattern in its simplest form.

#### A1 — Live Infrastructure Surgery (ports 8100–8107)

| Demo | Port | What it shows |
|------|------|---------------|
| [A1.1 Live SSL cert rotation](A1_Live_Infrastructure_Surgery/A1.1_Live_SSL_cert_rotation/) | 8100/8101 | `server.ssl.setCertificate(cert, key)` — hot-swap cert with zero connection drop |
| [A1.2 Add virtual host at runtime](A1_Live_Infrastructure_Surgery/A1.2_Add_a_virtual_host_at_runtime/) | 8102 | `nginx.http.addServer(name)` — new vhost live in one JS call |
| [A1.3 Remove virtual host on zero traffic](A1_Live_Infrastructure_Surgery/A1.3_Remove_a_virtual_host_on_zero_traffic/) | 8105 | `nginx.http.removeServer(srv)` — drain + remove with no restart |
| [A1.4 Config snapshot + rollback](A1_Live_Infrastructure_Surgery/A1.4_Config_snapshot_rollback/) | 8106 | `loc.snapshot()` / `snapshot.restore()` — one-click rollback |
| [A1.5 Feature-flag a location](A1_Live_Infrastructure_Surgery/A1.5_Feature_flag_a_location/) | 8107 | Toggle `/beta/` on/off by flipping a JS boolean; no reload |

#### A2 — Cross-Worker Coordination (ports 8108–8117)

| Demo | Port | What it shows |
|------|------|---------------|
| [A2.1 Global in-memory rate counter](A2_Cross_Worker_Coordination/A2.1_Global_in_memory_rate_counter/) | 8108 | `SharedArrayBuffer` + `Atomics.add` — lock-free counter shared across workers |
| [A2.2 Cross-worker broadcast](A2_Cross_Worker_Coordination/A2.2_Cross_worker_broadcast/) | 8109 | `nginx.broadcast(fn)` + `nginx.shared` — config pushed to all workers at once |
| [A2.3 Persistent background thread](A2_Cross_Worker_Coordination/A2.3_Persistent_background_thread/) | 8110 | `SharedWorker` with `setInterval` pre-computes data; workers read via postMessage |
| [A2.4 Self-adjusting canary](A2_Cross_Worker_Coordination/A2.4_Self_adjusting_canary/) | 8111 | SharedWorker counts 5xx, shifts weight from bad → good upstream automatically |
| [A2.5 SharedWorker compute offload](A2_Cross_Worker_Coordination/A2.5_Atomics_wait_notify_barrier/) | 8114 | Offloads fib(n) to a `SharedWorker`: SAB carries the data, the channel (`postMessage`) carries the signal — cross-process `Atomics.notify` is inert, so it is not used |
| [A2.6 Runtime route enable/disable](A2_Cross_Worker_Coordination/A2.6_Runtime_route_broadcast/) | 8115 | `nginx.broadcast` + `nginx.shared` flag — route live/dead on all workers instantly, no reload |
| [A2.7 Admin plugin: snapshot/rollback](A2_Cross_Worker_Coordination/A2.7_Admin_plugin_snapshot_rollback/) | 8116 | `nginx.use()` plugin with REST API, snapshots, rollback, and SharedWorker fan-out for structural ops |
| [A2.8 Live header mutation](A2_Cross_Worker_Coordination/A2.8_Live_header_mutation/) | 8117 | Two approaches side by side: `nginx.shared` + WebSocket for request-phase headers; `loc.headers.addHeader()` + cfgbus SharedWorker for config-phase `add_header` on a JS-handler-free location — both propagate to all 4 workers instantly, no reload |
| [A2.9 Local worker pool + Atomics barrier](A2_Cross_Worker_Coordination/A2.9_Local_worker_pool_barrier/) | 8112 | Pool of local JS `Worker`s shares a SAB and syncs with an intra-process `Atomics` barrier (where `notify` works); worker 0 folds the result into a `SharedWorker` global aggregate over the channel — the positive counterpart to A2.5 |

#### A3 — Programmatic Config Generation (ports 8118–8120)

| Demo | Port | What it shows |
|------|------|---------------|
| [A3.1 Multi-tenant from JSON](A3_Programmatic_Config_Generation/A3.1_Multi_tenant_from_JSON/) | 8115–8117 | `js_preprocess` reads `tenants.json`, emits one `server{}` per tenant |
| [A3.2 Environment-driven config](A3_Programmatic_Config_Generation/A3.2_Environment_driven_config/) | 8118/8119 | Same `nginx.conf` + `gen.js` behaves differently per `APP_ENV` |
| [A3.3 Config dry-run](A3_Programmatic_Config_Generation/A3.3_Config_dry_run/) | 8120 | `nginx.http.match(uri)` — simulate which location would match before deploying |

#### A4 — Interactive Operations (requires admin UI)

| Demo | Notes |
|------|-------|
| [A4.1 Browser REPL](A4_Interactive_Operations/A4.1_Browser_REPL/) | Requires `admin_ui_demo_v2` — see README |
| [A4.2 Per-worker REPL relay](A4_Interactive_Operations/A4.2_Per_worker_REPL_relay/) | Requires `admin_ui_demo_v2` — see README |
| [A4.3 COM object tree browser](A4_Interactive_Operations/A4.3_COM_object_tree_browser/) | Requires `admin_ui_demo_v2` — see README |

---

### B — More Convenient With JS

#### B1 — Request Routing (ports 8130–8134)

| Demo | Port | What it shows |
|------|------|---------------|
| [B1.1 Complex conditional routing](B1_Request_Routing/B1.1_Complex_conditional_routing/) | 8130 | Multi-condition: tenant + URI prefix + Accept header — 3 maps replaced by one `if` |
| [B1.2 Route on request body](B1_Request_Routing/B1.2_Route_on_request_body/) | 8133 | `await r.readBody()` — GraphQL operation name decides upstream |
| [B1.3 A/B test user segment](B1_Request_Routing/B1.3_AB_test_user_segment/) | 8134 | Cookie + time-of-day segmentation impossible with `split_clients` |

#### B2 — Authentication & Security (ports 8135–8137)

| Demo | Port | What it shows |
|------|------|---------------|
| [B2.1 Inline JWT validation](B2_Authentication_and_Security/B2.1_Inline_JWT_validation/) | 8135 | Decode + verify JWT in-process; zero network hop vs `auth_request` |
| [B2.2 Dynamic API-key blocklist](B2_Authentication_and_Security/B2.2_Dynamic_API_key_blocklist/) | 8136 | SharedWorker maintains revoked-keys set; check in < 1 µs |
| [B2.3 HMAC request signing](B2_Authentication_and_Security/B2.3_HMAC_request_signing/) | 8137 | Add `Authorization: HMAC-SHA256` to outbound requests — impossible without Lua |

#### B3 — Response Transformation (ports 8138–8140)

| Demo | Port | What it shows |
|------|------|---------------|
| [B3.1 JSON field masking](B3_Response_Transformation/B3.1_JSON_field_masking/) | 8138 | `addBodyFilter` parses JSON, masks `ssn`, removes `internal` field |
| [B3.2 Conditional HTML injection](B3_Response_Transformation/B3.2_Conditional_HTML_injection/) | 8139 | Inject `<script>` only for logged-in users; `sub_filter` ignores request context |
| [B3.3 Response fan-out / fan-in](B3_Response_Transformation/B3.3_Response_fan_out_fan_in/) | 8140 | 3 subrequests → merged JSON; SSI can fan-out but can't merge JSON |

#### B4 — Upstream Management (ports 8141–8147)

| Demo | Port | What it shows |
|------|------|---------------|
| [B4.1 Custom health check](B4_Upstream_Management/B4.1_Custom_health_check/) | 8141 | SharedWorker pings backends with custom protocol; marks `peer.down` |
| [B4.2 Sticky sessions](B4_Upstream_Management/B4.2_Sticky_sessions/) | 8144 | Hash by `X-Session-Token` header, falling back to JWT `sub` |
| [B4.3 DNS-refreshed upstream](B4_Upstream_Management/B4.3_DNS_refreshed_upstream/) | 8147 | SharedWorker re-resolves hostnames every N seconds, updates peer list |

#### B5 — Logging & Observability (ports 8148–8150)

| Demo | Port | What it shows |
|------|------|---------------|
| [B5.1 Request tracing](B5_Logging_and_Observability/B5.1_Request_tracing/) | 8148 | Generate/propagate `X-Request-Id`; enrich in hook, expose in response |
| [B5.2 Structured JSON log](B5_Logging_and_Observability/B5.2_Structured_JSON_log/) | 8149 | Emit `{"ts":…,"method":…}` per request; `log_format` is a static string |
| [B5.3 Per-request metrics](B5_Logging_and_Observability/B5.3_Per_request_metrics/) | 8150 | SAB latency histogram exposed as Prometheus text at `/metrics/` |

---

### C — More Efficient Than Classic nginx

#### C1 — Latency (ports 8160–8162)

| Demo | Port | What it shows |
|------|------|---------------|
| [C1.1 In-process JWT cache](C1_Latency/C1.1_In_process_JWT_cache/) | 8160 | SAB LRU: skip all crypto on cache hit — < 1 µs vs 0.5–20 ms `auth_request` |
| [C1.2 Parallel subrequests](C1_Latency/C1.2_Parallel_subrequests/) | 8161 | 3 internal subrequests merged; SSI serial = tA+tB+tC, JS = max(tA,tB,tC) |
| [C1.3 Pre-computed routing table](C1_Latency/C1.3_Pre_computed_routing_table/) | 8162 | SharedWorker builds route trie once; handlers do O(1) lookup, no lock, no reload |

#### C2 — Memory (ports 8163–8165)

| Demo | Port | What it shows |
|------|------|---------------|
| [C2.1 Shared routing state](C2_Memory/C2.1_Shared_routing_state/) | 8163 | One `nginx.shared` holds routing table for all workers — N workers, 1 copy |
| [C2.2 Streaming body transform](C2_Memory/C2.2_Streaming_body_transform/) | 8164 | GENERATOR filter strips comment lines chunk-by-chunk; never buffers full body |
| [C2.3 Connection-level session state](C2_Memory/C2.3_Connection_level_session_state/) | 8165 | `r.ctx` is per-request scratch freed by GC; no persistent pool overhead |

#### C3 — Operational Efficiency (ports 8166–8171)

| Demo | Port | What it shows |
|------|------|---------------|
| [C3.1 Hot-load new feature](C3_Operational_Efficiency/C3.1_Hot_load_new_feature/) | 8166 | Swap handler code at runtime; zero downtime, instant rollback available |
| [C3.2 Canary with automatic rollback](C3_Operational_Efficiency/C3.2_Canary_with_automatic_rollback/) | 8167 | SharedWorker monitors 5xx rate; reverts upstream weight if threshold exceeded |
| [C3.3 Config change audit trail](C3_Operational_Efficiency/C3.3_Config_change_audit_trail/) | 8170 | Every structural change logged with who/when/what via `r.log()` + `nginx.shared` |
| [C3.4 Plugin version pinning](C3_Operational_Efficiency/C3.4_Plugin_version_pinning/) | 8171 | `nginx.install({name:'acmecorp/auth@1.2.3', install: fn})` — semver-pinned, auditable |

#### C4 — Developer Velocity (qjs-based, ports 8172–8174)

| Demo | Port | What it shows |
|------|------|---------------|
| [C4.1 Unit test routing rules](C4_Developer_Velocity/C4.1_Unit_test_routing_rules/) | 8172 | `qjs route-test.js` — test routing logic without running nginx |
| [C4.2 Shared logic with frontend](C4_Developer_Velocity/C4.2_Shared_logic_with_frontend/) | 8173 | Same `normalize.js` module imported by nginx handler AND browser (zero duplication) |
| [C4.3 Config linting without nginx](C4_Developer_Velocity/C4.3_Config_linting_without_nginx/) | 8174 | `qjs validate-config.js` — JSON schema validation before loading; no binary needed |

---

### D — Audience-Specific Highlights

#### D1 — For SaaS / Platform Teams (ports 8175–8179)

| Demo | Port | What it shows |
|------|------|---------------|
| [D1.1 Tenant onboarding](D1_For_SaaS_Platform_Teams/D1.1_Tenant_onboarding/) | 8175 | POST → new vhost with SSL + upstream live in < 100 ms |
| [D1.2 Blue/green atomic switchover](D1_For_SaaS_Platform_Teams/D1.2_Blue_green_atomic_switchover/) | 8176 | SAB flag flipped atomically; all workers switch in one event-loop tick |
| [D1.3 Cross-worker quota enforcement](D1_For_SaaS_Platform_Teams/D1.3_Cross_worker_quota_enforcement/) | 8179 | SAB token-bucket: request rejected the moment global counter hits zero |

#### D2 — For Security / Compliance Teams (ports 8180–8182)

| Demo | Port | What it shows |
|------|------|---------------|
| [D2.1 Zero-trust gateway](D2_For_Security_Compliance_Teams/D2.1_Zero_trust_gateway/) | 8180 | Every request validated against OIDC-style token in-process; no sidecar |
| [D2.2 Dynamic IP blocklist](D2_For_Security_Compliance_Teams/D2.2_Dynamic_IP_blocklist/) | 8181 | `nginx.shared` CIDR store; hook rejects matching IPs in < 2 µs |
| [D2.3 WAF-lite streaming scanner](D2_For_Security_Compliance_Teams/D2.3_WAF_lite_streaming_scanner/) | 8182 | Hook scans request body for SQLi/XSS patterns; short-circuits with 400 |

#### D3 — For Architects / Engineering Managers (ports 8183–8185)

| Demo | Port | What it shows |
|------|------|---------------|
| [D3.1 Plugin marketplace model](D3_For_Architects_Engineering_Managers/D3.1_Plugin_marketplace_model/) | 8183 | `nginx.install()` composes rate-limit + CORS + telemetry plugins; `nginx.plugins[]` lists them |
| [D3.2 Single-pane admin UI](D3_For_Architects_Engineering_Managers/D3.2_Single_pane_admin_UI/) | — | Requires `admin_ui_demo_v2`; see README |
| [D3.3 Dev/prod parity](D3_For_Architects_Engineering_Managers/D3.3_Dev_prod_parity/) | 8184/8185 | Same `nginx.conf` + `gen.js` emits dev or prod config based on `APP_ENV` |

#### D4 — For L4 / Network Engineers (ports 8190–8192)

| Demo | Port | What it shows |
|------|------|---------------|
| [D4.1 Protocol-aware stream routing](D4_For_L4_Network_Engineers/D4.1_Protocol_aware_stream_routing/) | 8190 | Route by `X-Protocol` header (HTTP demo); README shows real `stream{}` + ClientHello inspection |
| [D4.2 Dynamic port listener](D4_For_L4_Network_Engineers/D4.2_Dynamic_port_listener/) | 8191 | `nginx.http.addServer()` provisions services at runtime; README shows `nginx.createSocket()` |
| [D4.3 TLS fingerprinting](D4_For_L4_Network_Engineers/D4.3_TLS_fingerprinting/) | 8192 | Classify bots vs browsers via JA3 fingerprint + UA (HTTP demo); README covers real TLS inspection |

---

## Notes

- **Ports**: all demos use dedicated ports in the 8100–8192 range. No conflicts with
  `js_com_demos` (8070–8087).
- **A4 / D3.2**: require the separate admin UI application (`admin_ui_demo_v2`).
  Each has a README explaining setup.
- **C4.1–C4.3**: primarily `qjs`-based; run `qjs <script>` without starting nginx.
  Each also exposes an HTTP endpoint for completeness.
- **D4**: HTTP simulations of L4 concepts. The real stream-module implementation is
  described in each demo's README.
