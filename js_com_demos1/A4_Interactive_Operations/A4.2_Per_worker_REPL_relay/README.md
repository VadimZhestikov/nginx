# A4.2 — Per-Worker REPL Relay

## What this demo shows

With multiple worker processes each worker runs its own copy of the JS context
(copy-on-write after fork). The Per-Worker REPL Relay allows an admin to:

1. Send a JS expression to **a specific worker** by index.
2. Evaluate it in that worker's private context.
3. Relay the result back to the browser.

This is essential for inspecting or modifying worker-private state (e.g.,
per-worker caches, local variables, SAB views).

## Classic nginx approach

Classic nginx workers are isolated processes with no inter-process JS
communication. There is no mechanism to query or mutate the state of a specific
worker at runtime.

## Architecture

```
Browser → admin HTTP endpoint
  → SharedWorker relay (master process pthread)
    → target worker's message channel (AF_UNIX SOCK_SEQPACKET)
      → eval in worker JS context
        → result returned through the same path
          → browser
```

## How to run

This demo requires the admin UI application in `js_com_apps/`. See the admin
UI README for setup instructions.

## Key concept

```javascript
// From the REPL — send to worker 1 specifically:
await relay.evalInWorker(1, `
    nginx._workerPrivate = nginx._workerPrivate || {};
    nginx._workerPrivate.queryCount++;
    nginx._workerPrivate.queryCount;
`);
// → 1   (only worker 1's counter was incremented)
```
