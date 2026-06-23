# A4.1 — Snapshot-centric Admin Console

**Layer 3** — the operator-facing console that composes all three layers of the
dynamic-reconfiguration plan into one screen:

| Layer | Contribution to the console |
|---|---|
| **L1** `nginx.describe()` | Every snapshot op is annotated with its safety class — rendered 🟢 safe / 🟡 guarded / 🔴 irreversible |
| **L2** the safety gate | Applying a snapshot that contains irreversible ops is blocked unless explicitly confirmed |
| **A2.7 engine** | Snapshots, apply / rollback / squash, reset-to-base, SharedWorker fan-out — reused verbatim |

…plus the signature Layer 3 feature: **the COW trap, made visible.** Each
worker stamps the snapshot id it has applied into `nginx.shared`; the console's
convergence panel shows which worker PIDs have caught up to the desired
snapshot, so a half-propagated change is obvious instead of silent.

## What it shows

```
┌── Snapshots ──┐┌── Snapshot detail ───────────────┐┌── Worker convergence ──┐
│ 0001-mixed  ★ ││ 🟢 routes.products = 1  auto-shared││ desired: 0001-mixed    │
│ 0002-danger   ││ 🟢 demo_backend/…weight zoned-shared││ worker 0 @ 0001 ✓      │
│               ││ 🟢 …addHeaders          worker-local││ worker 1 @ 0001 ✓      │
│ [Squash][Roll]││ 🟡 addLocation /dynamic/ guarded   ││ worker 2 @ 0001 ✓      │
│               ││ [Apply]                            ││ worker 3 @ base ⟳ lag  │
└───────────────┘└────────────────────────────────────┘└────────────────────────┘
```

Selecting `0002-danger` (a `removeLocation`) and pressing **Apply** pops a
confirmation because the op is 🔴 irreversible — the console-level expression of
Layer 2's tiers.

## File layout

```
nginx.conf      4-worker server on :8135 (reuseport); demo_backend (zoned) upstream
handler.js      loads admin-plugin; console REST + describe() annotation + gate + serves the SPA
console.html    the single-page console (history · colored ops · convergence panel)
admin-plugin/   the A2.7 snapshot engine, verbatim + a convergence stamp at the apply seam
snapshots/      snapshot JSON written at runtime
test.sh         starts nginx, drives the console end-to-end (18 checks)
```

## Running

```bash
cd js_com_demos/A4_Interactive_Operations/A4.1_Admin_console
bash test.sh                       # 18 checks
# interactively:
../../../objs/nginx -p . -c nginx.conf
xdg-open http://127.0.0.1:8135/    # the console
```

## REST API (consumed by the SPA)

| Method | Path | Description |
|---|---|---|
| `GET`  | `/` | The single-page console |
| `GET`  | `/c/snapshots` | `{ snapshots, desired }` |
| `GET`  | `/c/snapshot?id=` | Snapshot ops, each **annotated** with `class` / `propagation` / `reversible` / `note` |
| `GET`  | `/c/state` | `admin.state()` (live managed values) |
| `GET`  | `/c/converge` | `{ desired, workers:[{idx,at,converged}], allConverged }` |
| `POST` | `/c/apply?id=[&confirm=1]` | Apply; **409** (with the blocking ops) if irreversible and not confirmed |
| `POST` | `/c/rollback` | Roll back one step |
| `POST` | `/c/squash?ids=a,b&name=` | Merge snapshots |
| `POST` | `/c/snapshot/create?name=` | Snapshot the current state |
| `POST` | `/c/raw?name=&ops=` | Stage a snapshot from an explicit ops array |

The admin plugin's own `/admin/` REST is also mounted (bonus).

## How convergence works

`admin.applySnapshot(id)` records the desired id in `nginx.shared`
(`sc.cv.desired`) and stamps the applying worker (`sc.cv.<workerIdx> = id`). The
SharedWorker fan-out delivers the snapshot to every other worker, whose receiver
applies it and stamps its own `sc.cv.<workerIdx>`. `/c/converge` reads all
stamps and compares each to `sc.cv.desired` — a worker still showing an older id
is flagged as lagging. (The stamp is the only change made to the otherwise
verbatim A2.7 engine.)

## The three layers, on one screen

This demo is the apex of the plan in `js_com_docs/js-com-safety-classes.adoc`:

1. **L1** `A0.2_Safety_class_inspector` — read the classes.
2. **L2** `A0.3_Safe_config_actuator` — act through the safety gateway.
3. **L3** *this* — the operator console: history, colored diffs, the gate, and
   cross-worker convergence in one place.
