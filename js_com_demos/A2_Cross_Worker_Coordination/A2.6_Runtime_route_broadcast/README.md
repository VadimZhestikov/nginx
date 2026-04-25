# A2.6 — Runtime Route Enable/Disable, Instantly Visible to All Workers

Demonstrates how `nginx.broadcast()` and `nginx.shared` together replicate the
effect of `nginx -s reload` — activating or deactivating a route across the
entire worker pool — without restarting any worker or dropping a single
in-flight connection.

## What it shows

| Primitive | Role in this demo |
|---|---|
| `nginx.broadcast(fn)` | Installs the same route handlers in all 4 workers with one call at init-conf time |
| `nginx.shared` | Lock-free shared-memory flag — one worker writes, all workers read the new value on their very next request |

Standard nginx requires `nginx -s reload` to activate a new `location` block.
That respawns all workers and briefly interrupts long-lived connections.  Here,
enabling or disabling `/api/v2/` is a single shared-memory write: the change is
visible to every worker before the writing worker's response even leaves the
kernel.  No sleep, no IPC, no reload.

## File layout

```
nginx.conf      4-worker server on :8115 (reuseport)
handler.js      js_source: broadcast installs admin + app handlers in every worker
test.sh         Self-contained test (starts nginx, runs all checks, stops nginx)
```

## Running

```bash
cd js_com_demos/A2_Cross_Worker_Coordination/A2.6_Runtime_route_broadcast
bash test.sh
```

## Endpoints

| Method | Path | Description |
|---|---|---|
| `POST` | `/admin/add-route/` | Set `routes.api_v2 = "1"` in shared memory — `/api/v2/` goes live on all workers |
| `POST` | `/admin/remove-route/` | Set `routes.api_v2 = "0"` — `/api/v2/` returns 404 on all workers |
| `GET` | `/api/v2/` | Returns 200 + JSON while the flag is `"1"`, 404 while `"0"` |
| `GET` | `/status/` | Responding worker PID + list of currently enabled routes |

Both admin operations are idempotent.

## How it works

```
nginx.broadcast(fn)          ← called once, in master at init-conf time
        │
        ├─▶ worker 1 init_process: installs handlers, seeds flag = "0"
        ├─▶ worker 2 init_process: installs handlers (flag already set, skips seed)
        ├─▶ worker 3 init_process: installs handlers
        └─▶ worker 4 init_process: installs handlers

POST /admin/add-route/       ← handled by whichever worker accepts the connection
        │
        └─▶ nginx.shared.set('routes.api_v2', '1')
                │
                ├─▶ worker 1: next nginx.shared.get() → "1"  (no IPC)
                ├─▶ worker 2: next nginx.shared.get() → "1"
                ├─▶ worker 3: next nginx.shared.get() → "1"
                └─▶ worker 4: next nginx.shared.get() → "1"
```

The `/api/v2/` handler reads `routes.api_v2` on every request.  There is no
coordinator, no lock, and no per-worker notification — the route is live on all
workers the instant the flag is written.

## Shared-memory flag seeding

The first worker to run its broadcast callback seeds the flag:

```js
if (nginx.shared.get('routes.api_v2') === undefined) {
    nginx.shared.set('routes.api_v2', '0');
}
```

Subsequent workers skip the write because the key already exists.  This
first-write-wins pattern is safe because `nginx.shared` operations are atomic
and workers start sequentially from the master.

## Comparison with nginx -s reload

| | `nginx -s reload` | This demo |
|---|---|---|
| New route active after | ~100–500 ms (respawn) | < 1 µs (shared-memory write) |
| In-flight connections | Gracefully handed off, but new workers are empty | Unaffected |
| Worker restart | Yes | No |
| Suitable for | Config-file changes, SSL cert rotation | Runtime feature flags, A/B toggles, circuit breakers |
