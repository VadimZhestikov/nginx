# admin_snapshot_rollback

A self-contained nginx admin application built on the JS COM API that provides
live config snapshot and rollback without a process reload.

## Concept

nginx starts from a static `nginx.conf`.  At runtime operators mutate the live
config (upstream peer weights, location handlers, …) through the admin API.
Each mutation can be captured as a named **snapshot** — a delta from the base
`nginx.conf` state stored as a JSON file under `snapshots/`.  Snapshots can be
applied or rolled back at any time; the last applied snapshot is remembered
across restarts.

```
snapshots/
  0001-initial.json   ← oldest
  0002-add-canary.json
  0003-weight-shift.json  ← current (pinned)
```

### Snapshot format

```json
{
  "id": "0003-weight-shift",
  "ts": 1710000000,
  "peers": [
    { "upstream": "backend", "peer": "127.0.0.1:8091", "weight": 10 },
    { "upstream": "backend", "peer": "127.0.0.1:8092", "weight":  1 }
  ],
  "handlers": []
}
```

## Admin HTTP API

| Method | Path                          | Action                              |
|--------|-------------------------------|-------------------------------------|
| GET    | /admin/snapshots              | List available snapshots            |
| POST   | /admin/snapshots              | Create snapshot of current state    |
| GET    | /admin/snapshots/:id          | Show snapshot content               |
| POST   | /admin/apply/:id              | Apply snapshot (rolls back current) |
| POST   | /admin/rollback               | Roll back to previous snapshot      |
| GET    | /admin/state                  | Show current live config state      |

## Files

| File              | Purpose                                    |
|-------------------|--------------------------------------------|
| `nginx.conf`      | Base nginx configuration                   |
| `conf/admin.js`   | Admin application (JS COM)                 |
| `snapshots/`      | Persisted snapshot JSON files              |
| `t/`              | Test suite                                 |

## Running

```bash
# from the nginx build directory
nginx/objs/nginx -c $(pwd)/js_com_apps/admin_snapshot_rollback/nginx.conf \
                 -p $(pwd)/js_com_apps/admin_snapshot_rollback
```

## Implementation plan

- **Commit 3** — `admin.js` core: base-state capture, peer/handler sync,
  `applySnapshot`, broadcast wiring.  Test: `t/js_admin_base.t`.
- **Commit 4** — Snapshot creation (`createSnapshot`).
  Test: `t/js_admin_snapshot.t`.
- **Commit 5** — Rollback (`rollback`, `pin`).
  Test: `t/js_admin_rollback.t`.
- **Commit 6** — HTTP admin API handlers.
  Test: `t/js_admin_api.t`.
