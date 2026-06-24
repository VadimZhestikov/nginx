# A0.2 — COM Safety-Class Inspector

Walks the live nginx COM tree via `nginx.describe()` and renders a
**traffic-light report** of every settable property/method — the read-only
seed of the Layer 3 admin console.

For each member it shows the three orthogonal safety axes:

| Axis | Values |
|---|---|
| **class** | 🟢 `safe` · 🟡 `guarded` · 🔴 `irreversible` · ⚪ `readonly` |
| **propagation** | `worker-local` · `zoned-shared` · `auto-shared` |
| **requestScoped** | honours `setWriteMode('local')` for a per-request override |

The class set is **not hard-coded**: `nginx.describe()` with no path is the
*discovery root* (it returns every classifiable COM class), and `describe(path)`
now lists read-only getters alongside settable members — so the report is a
complete per-object reference, and the HTML shows a class-catalog panel.

This lets an operator see — without reading any C or JS source — which runtime
changes are safe, which need cross-worker fan-out, and which can never be
undone.

## What it shows

| COM primitive | Role in this demo |
|---|---|
| `nginx.describe()` | The discovery root: `[{class, members:[…]}]` for every classifiable COM class |
| `nginx.describe(path)` | Safety descriptor array — settable members **and** read-only getters — for a COM object |
| `nginx.describe(path, name)` | Single-member descriptor (or `null`) |
| the per-class_id registry | One generic walk classifies `nginx.http`, `cycle`, locations, sub-objects, upstream peers, and `events` |

Key thing the report makes visible: the **same** `peer.weight =` write is
`zoned-shared` on the `zoned` upstream (it has a `zone`, so the change reaches
every worker under the rr-peers lock) but `worker-local` on the `plain`
upstream (no zone → the write lands in the calling worker only and needs
SharedWorker fan-out to propagate). This is the COW trap, surfaced.

## File layout

```
nginx.conf   2-worker server on :8132; zoned + plain upstreams; a proxied location
handler.js   walks the COM tree via nginx.describe(); JSON + HTML endpoints
test.sh      starts nginx, exercises all endpoints (27 checks), stops nginx
```

## Running

```bash
cd js_com_demos/A0_COM_API_Extension/A0.2_Safety_class_inspector
bash test.sh                       # 27 checks
# or, interactively:
../../../objs/nginx -p . -c nginx.conf
xdg-open http://127.0.0.1:8132/    # HTML traffic-light table
```

## Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | HTML traffic-light table + class-catalog panel |
| `GET` | `/catalog` | JSON discovery root: `[{class, members:[…]}]` for every class |
| `GET` | `/inspect` | Full JSON report — array of `{path, members:[…]}` |
| `GET` | `/inspect/sum` | JSON summary: counts by class + propagation + request-scoped |

Example summary (now includes `nginx.http`'s irreversible topology methods and
read-only getters):

```json
{
 "total": 1166,
 "byClass":       { "safe": 758, "guarded": 39, "irreversible": 2, "readonly": 367 },
 "byPropagation": { "worker-local": 1156, "zoned-shared": 10, "auto-shared": 0 },
 "requestScoped": 335
}
```

(`nginx.describe()` exposes **54** classes; the irreversible pair is
`nginx.http.addServer` / `attach`.)

## How it relates to the design

This demo is **Layer 1** of the operator-facing reconfiguration plan
(`js_com_docs/js-com-safety-classes.adoc`):

1. **Layer 1 — safety-class metadata** (`nginx.describe()`): this demo.
2. **Layer 2 — policy plugins**: assert on `class` before acting; auto-wire
   fan-out for `worker-local` writes; refuse `irreversible` ops as casual toggles.
3. **Layer 3 — admin console**: the snapshot-centric UI from
   `A2.7_Admin_plugin_snapshot_rollback`, with each pending op tagged by the
   `class` colour shown here and gated on convergence/ACK state.
