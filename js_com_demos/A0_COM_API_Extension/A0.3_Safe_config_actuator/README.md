# A0.3 — Safe-Config Actuator

**Layer 2** in action: a REST actuator over `nginx.safeConfig`, the policy
plugin that turns raw COM mutations into vetted, safety-enforced operations.

Where A0.2 *reads* the safety classes (the inspector), A0.3 *acts* on them. Every
change flows through a gateway that:

1. consults `nginx.describe()` (Layer 1) for the target's class + propagation,
2. enforces a **per-tier opt-in** — `safe` proceeds; `guarded` needs `{ack:true}`;
   `irreversible` needs `{confirm:true}`,
3. **routes by propagation** — `worker-local` writes are fanned out to every
   worker via a SharedWorker; `zoned-shared` / `auto-shared` apply directly.

Operators call vetted operations and never touch a raw COM path or have to know
which writes need cross-worker fan-out.

## What it shows

| Primitive | Role |
|---|---|
| `nginx.use('safe_config', {cfgWorkerPath})` | Loads the standalone plugin; installs `nginx.safeConfig` |
| `nginx.describe()` (Layer 1) | The gateway reads class/propagation before every write |
| SharedWorker fan-out | `worker-local` writes (headers, handler toggles) reach all 4 workers |
| zoned-shared via shm | peer-weight / drain changes are visible to all workers with no fan-out |

## File layout

```
nginx.conf   4-worker server on :8134 (reuseport); zoned upstream; REST endpoints
handler.js   nginx.use(safe_config) + REST actuator over nginx.safeConfig
test.sh      starts nginx, drives the gateway, verifies cross-worker propagation (14 checks)
```

The plugin itself lives at `js_com_apps/safe_config/` (reusable, also covered by
`t/js_safe_config.t`); the demo loads it by relative path.

## Running

```bash
cd js_com_demos/A0_COM_API_Extension/A0.3_Safe_config_actuator
bash test.sh                       # 14 checks
# interactively:
../../../objs/nginx -p . -c nginx.conf
curl -s 'http://127.0.0.1:8134/gate-demo'        # see a guarded write rejected then accepted
curl -s -X POST 'http://127.0.0.1:8134/canary?pct=30'
curl -s 'http://127.0.0.1:8134/weights'          # 70/30 on every worker
```

## Endpoints

| Method | Path | Operation |
|---|---|---|
| `GET`  | `/plan?path=&value=` | Dry-run: class / propagation / would-fan-out (no mutation) |
| `POST` | `/canary?pct=N` | `canaryWeight('zoned', N)` — N% of traffic to the canary peer |
| `POST` | `/header?key=&value=` | `setResponseHeader` on `/app` (worker-local → fanned out) |
| `POST` | `/toggle?on=0\|1` | `toggleLocation` `/app` — 503 when off, restored when on |
| `POST` | `/drain?addr=` | `drainPeer('zoned', addr)` — mark a peer down |
| `GET`  | `/gate-demo` | A guarded write rejected without ack, then accepted with `{ack:true}` |
| `GET`  | `/weights` | Responding worker pid + live zoned peer weights |
| `GET`  | `/app` | App route (carries the header; 503 while toggled off) |

## The gate, demonstrated

`GET /gate-demo` applies a `guarded` member (`proxy.pass`) twice:

```json
{
  "withoutAck": "rejected: safeConfig: \"…proxy.pass\" is guarded
                 (Re-targets the upstream for this location); pass {ack:true} to proceed",
  "withAck":    { "class": "guarded", "propagation": "worker-local",
                  "fannedOut": true, "applied": true }
}
```

## Cross-worker propagation

* **`canaryWeight` / `drainPeer`** (zoned-shared) — the `zoned` upstream has a
  `zone`, so peer-weight and down writes land in shared memory and every worker
  sees them with **no fan-out**. `/weights` returns `70/30` from all four
  worker PIDs immediately after `/canary?pct=30`.
* **`setResponseHeader` / `toggleLocation`** (worker-local) — these mutate
  per-worker config, so the gateway **fans them out** via the SharedWorker. The
  `X-Demo` header and the 503 toggle appear on responses from every worker.

## Relation to the design

This is **Layer 2** of the operator-reconfig plan
(`js_com_docs/js-com-safety-classes.adoc`):

1. **Layer 1** — `nginx.describe()` safety metadata (demo `A0.2`).
2. **Layer 2** — `safe-config` policy plugin: this demo.
3. **Layer 3** — snapshot-centric admin console (`A2.7`), coloring each pending
   op by the class shown here and gating on cross-worker convergence.
