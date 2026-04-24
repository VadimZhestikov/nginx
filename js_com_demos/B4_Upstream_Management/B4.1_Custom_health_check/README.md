# B4.1 — Custom Health Check

## What it shows

A **SharedWorker** thread acts as an in-process health-check daemon, tracking
the up/down state of simulated upstream peers.  The `/status/` endpoint reports
current health; `/admin/toggle/` lets you simulate a peer going down or
recovering.

In a real deployment the SharedWorker would run `setInterval` to probe each
peer's `/_health` endpoint via HTTP every few seconds, and update the state
automatically without any admin call.

## Architecture

```
nginx worker (request handler)
    │
    │ postMessage({cmd:'status'})
    ▼
SharedWorker thread (sw.js)
    │  peers = [{addr, healthy}, ...]
    │
    └─ postMessage({peers: [...]})
```

## Classic nginx comparison

Active HTTP health checks are a **nginx Plus** (commercial) feature.  The
open-source version only does passive health detection (marks a peer down after
failed proxy attempts) via `proxy_next_upstream`.  Community workarounds require
the third-party `ngx_upstream_check_module` or an external health-check daemon
writing to shared memory.  With the JS SharedWorker, the health-check logic runs
inside the nginx process with no additional infrastructure.

## How to run

```bash
cd B4.1_Custom_health_check
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Check initial state — both peers healthy
curl http://localhost:8141/status/

# Simulate peer 8143 going down
curl -X POST "http://localhost:8141/admin/toggle/?addr=127.0.0.1:8143&healthy=false"
curl http://localhost:8141/status/

# Simulate peer 8143 recovering
curl -X POST "http://localhost:8141/admin/toggle/?addr=127.0.0.1:8143&healthy=true"
curl http://localhost:8141/status/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
