# A0.1 — Custom nginx Property via `nginx.use()`

The foundational pattern behind every demo in this collection.  Read this first.

## The core idea

Every demo adds something new to nginx at runtime — a new route, a rate counter,
a snapshot API, a canary weight.  All of them do it the same way:

```
nginx C core
    └── JS COM bindings  (nginx.http, nginx.shared, nginx.broadcast, nginx.use, …)
            └── user-defined JS API  (nginx.featureFlags — added by THIS demo)
                    └── application logic  (handlers that call nginx.featureFlags)
```

`nginx.use('./feature-flags', config)` evaluates the plugin module once, in
master, before any worker forks.  The module sets `nginx.featureFlags` — a new
property directly on the `nginx` object.  All workers inherit it via COW fork.
From that point on, `nginx.featureFlags` is indistinguishable from built-in
properties like `nginx.shared` or `nginx.http`.

## File layout

```
nginx.conf              4-worker server on :8099 (reuseport)
handler.js              js_source: loads plugin, installs app handlers
feature-flags/
  index.js              Plugin: sets nginx.featureFlags = { get, set, list }
  package.json          Plugin metadata (ngxjs.main)
test.sh                 Self-contained test (starts nginx, runs all checks, stops nginx)
```

## Running

```bash
cd js_com_demos/A0_COM_API_Extension/A0.1_Custom_nginx_property
bash test.sh
```

## What the plugin adds

`feature-flags/index.js` sets one new property on `nginx`:

| Method | Description |
|---|---|
| `nginx.featureFlags.get(name)` | Returns `true` / `false` |
| `nginx.featureFlags.set(name, enabled)` | Writes to `nginx.shared`; instantly visible to all workers |
| `nginx.featureFlags.list()` | Returns `{ name: true\|false, … }` for all managed flags |

The plugin uses `nginx.shared` internally for state — so a `set()` call on any
worker is immediately visible to every other worker with no IPC.

## Endpoints

| Method | Path | Description |
|---|---|---|
| `GET` | `/flags/` | List all flags with current values |
| `POST` | `/flags/enable/` | Body: `{"flag":"name"}` — enable a flag |
| `POST` | `/flags/disable/` | Body: `{"flag":"name"}` — disable a flag |
| `GET` | `/api/` | 200 while `new-checkout` is enabled, 404 otherwise |

## The two-step pattern

```js
// Step 1 — extend the nginx COM object (runs in master, before fork)
nginx.use('./feature-flags', { flags: ['dark-mode', 'new-checkout'] });

// Step 2 — install handlers that USE the extension (runs in every worker)
nginx.broadcast(function () {
    // nginx.featureFlags is available here exactly like nginx.shared
    nginx.http.servers[0].locations
        .find(function (l) { return l.path === '/api/'; })
        .handler = function (r) {
            if (!nginx.featureFlags.get('new-checkout')) {
                r.respond(404, {}, 'not found\n');
                return;
            }
            r.respond(200, {}, '{"ok":true}\n');
        };
});
```

Every other demo in this collection is an instance of this pattern:

| Demo | What `nginx.use()` / `nginx.broadcast()` adds |
|---|---|
| A1.x | `nginx.http.addServer`, `server.ssl.setCertificate`, … |
| A2.2 | Feature flags via `nginx.shared` (inline, no plugin) |
| A2.4 | Self-adjusting canary weight via SharedWorker |
| A2.7 | `nginx.admin` — full snapshot/rollback REST API |
| B2.1 | Inline JWT validation wired into a location handler |

## Why master context matters

`nginx.use()` runs at **init-conf time**, inside the master process, before any
worker forks.  This means:

- The plugin is evaluated exactly **once** — not once per worker.
- All workers receive `nginx.featureFlags` via **COW fork** — no re-evaluation,
  no file I/O at worker start.
- Handlers registered at init-conf time (via `nginx.admin.registerHandler` in
  A2.7, for example) are automatically available in every worker for the same
  reason.

If you called `nginx.use()` inside a request handler instead, each worker would
re-evaluate the plugin independently and the property would not be shared.
`nginx.broadcast()` is the correct way to run code in every worker after fork.
