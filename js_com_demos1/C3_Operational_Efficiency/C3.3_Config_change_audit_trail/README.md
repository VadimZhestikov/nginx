# C3.3 — Config Change Audit Trail

## What it shows

Every `POST /admin/change/` records a structured audit entry — timestamp,
user, action, and detail — into `nginx.shared`, making it visible to all
worker processes.  The last 20 entries are kept in a JSON circular buffer.
`GET /api/audit/` returns the full log.

Changes are also written to `nginx.log` at info level, giving you both
a queryable in-memory log and a persistent log-file record.

## Why classic NGINX cannot do this

Classic NGINX has no writable shared state accessible from request handlers.
`nginx.shared` provides a cross-worker dictionary that any JS handler can
read or write at request time, enabling features like audit logs, counters,
feature flags, and config state without external storage.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | 2-worker server on port 8170 |
| `handler.js` | `POST /admin/change/`, `GET /api/audit/` |
| `test.sh` | Automated test: post changes, retrieve log |

## How to run

```bash
cd C3.3_Config_change_audit_trail
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# Record a config change
curl -X POST -H "Content-Type: application/json" \
  -d '{"action":"add_location","user":"alice","detail":"/beta/"}' \
  http://localhost:8170/admin/change/

# Record another
curl -X POST -H "Content-Type: application/json" \
  -d '{"action":"update_upstream","user":"bob","detail":"pool_size=50"}' \
  http://localhost:8170/admin/change/

# Retrieve the audit log
curl http://localhost:8170/api/audit/ | python3 -m json.tool

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. POST changes from multiple users; each gets a monotonically increasing
   sequence number.
2. GET the audit log — all changes appear in order with timestamps.
3. With `worker_processes 2`, changes written by worker 1 are immediately
   visible to worker 2 (via `nginx.shared`).
4. Changes also appear in `logs/error.log` prefixed with `AUDIT [seq]`.
5. The circular buffer caps at 20 entries; older entries are evicted
   automatically.
