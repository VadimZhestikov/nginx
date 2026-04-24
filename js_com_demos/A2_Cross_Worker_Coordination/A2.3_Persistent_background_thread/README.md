# A2.3 — Persistent Background Thread

## What this demo shows

A background "ticker" using `nginx.setTimeout()` inside a `nginx.broadcast()`
callback. The ticker runs in worker 0 as a recurring async timer loop that
fires every 500 ms, independent of incoming requests. Its state is stored in
`nginx.shared` for visibility across all workers.

This pattern implements a persistent background task entirely in JS — no
external daemon, no OS thread, no cron job.

Use cases:
- Periodic metric aggregation / flush to a remote store
- Background cache refresh (fetch from origin every N seconds)
- Scheduled config reload from a file or external service
- Heartbeat updates for a distributed lock or lease

## Classic nginx approach

Classic nginx has no background task mechanism for JS logic. Solutions require
external daemons (cron, sidecars) that communicate via sockets or files, adding
operational complexity and deployment overhead.

## How it works

```javascript
nginx.broadcast(function() {
    function tick() {
        // do background work ...
        nginx.shared.set('bg.tick', String(++t));
        nginx.setTimeout(500).then(tick);   // reschedule
    }

    if (nginx.workerIdx === 0) {
        nginx.setTimeout(500).then(tick);   // start the loop in worker 0
    }
});
```

- `nginx.broadcast(fn)` runs `fn` in each worker after startup.
- `nginx.setTimeout(ms)` returns a Promise that resolves after `ms` milliseconds
  without blocking the event loop.
- Only worker 0 runs the ticker to avoid double-incrementing with multiple workers.
- `nginx.shared` stores results so any worker can read them on requests.

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Wait for the ticker to fire a couple of times
sleep 1.5
curl http://127.0.0.1:8110/value/
# → {"tick":3,"value":21}   (approximately)

# Manual tick
curl -X POST http://127.0.0.1:8110/tick/
# → {"tick":4,"value":28}

# Reset
curl -X POST http://127.0.0.1:8110/reset/
# → {"tick":0,"value":0}

# Ticker continues from 0
sleep 0.6
curl http://127.0.0.1:8110/value/
# → {"tick":1,"value":7}

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
