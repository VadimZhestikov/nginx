# admin_UI_demo_v2

A reference nginx JS application that combines a live `nginx.*` object browser
with a per-worker JavaScript REPL, all served from within nginx itself.

## Quick start

```bash
# From the repository root
objs/nginx -p . -c js_pilgrim_apps/admin_UI_demo_v2/nginx.conf

# Open in a browser
open http://localhost:8080/
```

Stop: `kill $(cat js_pilgrim_apps/admin_UI_demo_v2/logs/nginx.pid)`

Reload config: `kill -HUP $(cat js_pilgrim_apps/admin_UI_demo_v2/logs/nginx.pid)`

## URLs

| URL | Description |
|-----|-------------|
| `http://localhost:8080/` | Admin UI v2 (tree browser + REPL) |
| `http://localhost:8080/admin/` | Classic admin shell (single REPL) |
| `http://localhost:8080/info` | JSON: app info, worker index, upstream list |
| `http://localhost:8080/health` | JSON: `{"status":"ok","worker":<N>}` |
| `ws://localhost:8080/admin/ws` | WebSocket REPL (any worker) |
| `ws://localhost:8080/admin/ws?w=N` | WebSocket REPL pinned to worker N |

## Layout

```
admin_UI_demo_v2/
  nginx.conf          — server config (port 8080, 4 workers, 3 upstreams)
  init.js             — app entry point; seeds shared state, wires /info and /health
  plugins/
    banner/index.js   — response hook: X-Powered-By + X-Request-Id headers
    metrics/index.js  — request hook: per-location request counters
  static/
    index.html        — the Admin UI v2 single-page app
  logs/               — error.log, access.log, nginx.pid (git-ignored)
```

The app depends on the `admin-shell` package (sibling directory):

```
admin-shell/
  admin-shell.js      — WebSocket server, JSON-RPC dispatcher, cross-worker relay
  repl-relay.js       — SharedWorker that routes eval requests between workers
  static/index.html   — classic single-worker admin shell UI
```

## UI panels

### nginx.* Tree Browser (left panel)

Displays any JavaScript expression as an expandable tree.  The default root is
`nginx`.  Click the `▸` arrow next to any object or array to expand it; click
`▾` to collapse.  Type a different expression in the toolbar and press **Browse**
to navigate to it.  **Refresh** clears the cache and re-fetches from scratch.

The tree is backed by two JSON-RPC methods:

- `tree.get(expr)` — evaluates `expr` and returns type metadata
  (`{kind, value/keys/length, ...}`)
- `tree.getChildren(expr)` — returns all children of `expr` in one round-trip,
  avoiding N individual RPCs

### Per-Worker REPL (right panel)

One tab per worker process.  On page load all workers are auto-connected in a
staggered sequence (200 ms + 150 ms × worker index) to avoid thundering-herd on
the SharedWorker.

Each tab opens a WebSocket to `/admin/ws?w=N`.  The `?w=N` query parameter pins
the connection to worker N: if the HTTP request lands on a different worker, that
worker relays the eval to worker N via the `repl-relay` SharedWorker.

**Keyboard shortcuts in the input line:**

| Key | Action |
|-----|--------|
| Enter | Submit expression |
| Tab | Autocomplete (fills common prefix; shows columns if ambiguous) |
| ↑ / ↓ | Navigate command history |

The result of each eval is shown with a `←` prefix.  `console.log/warn/error`
output captured during the eval is displayed before the result, prefixed with
`[log]`, `[warn]`, or `[error]`.

Buttons: **Connect All** / **Disconnect All** operate on all worker tabs at once.

## JSON-RPC API

All WebSocket connections speak JSON-RPC 2.0 (text frames).

```json
{"jsonrpc":"2.0","id":1,"method":"<method>","params":[...]}
```

| Method | Params | Result |
|--------|--------|--------|
| `nginx.eval` | `[code]` | `{eval:{status,value,message,stack}, logs:[{lvl,msg}]}` |
| `repl.complete` | `[code]` | `{completions:[...], prefix}` |
| `tree.get` | `[expr]` | `{kind, value?, keys?, length?, name?}` |
| `tree.getChildren` | `[expr]` | `[{key, info:{...}}, ...]` |
| `plugins.list` | `[]` | `[...paths]` |
| `shared.get` | `[key]` | value string or `undefined` |
| `shared.set` | `[key, value]` | `"ok"` |
| `shared.delete` | `[key]` | `"ok"` |
| `shared.keys` | `[]` | `[...keys]` |
| `upstreams.list` | `[]` | `[{name, peers:[{address,weight,down,maxFails}]}]` |
| `worker.id` | `[]` | worker index integer |

`nginx.eval` status values: `"ok"` (value in `.value`), `"error"` (message +
optional stack), `"incomplete"` (expression not yet complete).

## Cross-worker eval

```
Browser  →  ws?w=2  →  Worker 0 (handles the connection)
                            │  relay.postMessage({type:'eval', targetWorker:2})
                            ▼
                      repl-relay SharedWorker (master process thread)
                            │  port[2].postMessage({type:'eval', ...})
                            ▼
                        Worker 2  →  nginx.repl.eval(code)
                            │  relay.postMessage({type:'result', replyWorker:0})
                            ▼
                      repl-relay SharedWorker
                            │  port[0].postMessage({type:'result', ...})
                            ▼
                        Worker 0  →  nginx.repl._writeFdRaw(fd, frame)
                            ▼
                          Browser  ←  JSON-RPC response
```

The SharedWorker is started after nginx daemonises so its pthread survives the
double-fork.  Each worker registers a port with `{type:'register', workerId:N}`
on startup.  Evals targeting an unregistered worker are queued in the SharedWorker
until that worker registers.

## Plugins

Plugins are loaded by `init.js` via `nginx.use(path)`.  Each plugin is a
self-contained IIFE that installs hooks or modifies locations at config time.

### banner plugin

Adds two response headers to every location:

- `X-Powered-By: nginx-js-pilgrim/<version>`
- `X-Request-Id: <monotonically incrementing integer>`

The request ID is stored in `nginx.shared` (key `banner:request_id`) so it
increments globally across all worker processes.

### metrics plugin

Increments a per-location request counter in `nginx.shared` on every request.
Keys follow the pattern `metrics:<path>:requests`.  Counters are seeded to `0`
on the first request so they appear in the tree browser before any traffic.

Example entries after some traffic:

```
nginx.shared.get('metrics:/:requests')          // "17"
nginx.shared.get('metrics:/admin/ws:requests')  // "4"
```

## Shared state

`nginx.shared` is a process-global key/value store (string → string) visible to
all workers.  `init.js` seeds the following keys once on the first request:

| Key | Example value |
|-----|---------------|
| `demo:app` | `"admin_UI_demo_v2"` |
| `demo:version` | `"2.0.0"` |
| `demo:environment` | `"development"` |
| `demo:started_at` | ISO timestamp |
| `demo:workers` | `"4"` |
| `config:log_level` | `"notice"` |
| `config:max_rps` | `"1000"` |
| `feature:beta_ui` | `"true"` |
| `feature:tracing` | `"false"` |

These can be read and written live from the REPL:

```js
nginx.shared.set('config:max_rps', '2000')
nginx.shared.get('feature:tracing')
```

## Upstreams

The config defines three upstream groups used as proxy targets:

| Upstream | Servers | Notes |
|----------|---------|-------|
| `web_backend` | :8091 (w=5), :8092 (w=3), :8093 (backup) | Round-robin |
| `api_backend` | :8094 (w=10), :8095 (w=5, max_fails=3) | Weighted |
| `cache_backend` | :8096 | Single peer |

The upstream objects are visible in the tree browser at `nginx.http.upstreams`
and via the `upstreams.list` RPC.

## Writing a plugin

A plugin is a JS file loaded via `nginx.use(path)` from `init.js`.  It runs
once in the master context during config initialisation, before workers fork.

```js
// plugins/my-plugin/index.js
(function() {
    // Install a response hook on every location
    var servers = nginx.http.servers;
    for (var si = 0; si < servers.length; si++) {
        servers[si].locations.forEach(function(loc) {
            loc.addResponseHook(function(req) {
                req.setHeader('X-My-Header', 'hello');
            });
        });
    }
    nginx.log(6, 'my-plugin: loaded');
}());
```

Then in `init.js`:

```js
nginx.use('js_pilgrim_apps/admin_UI_demo_v2/plugins/my-plugin');
```
