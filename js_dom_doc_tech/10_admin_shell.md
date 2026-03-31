# 10 — Admin Shell and REPL (P19)

## Purpose

P19 provides a browser-based admin UI reachable over WebSocket.  Any JS code
running in any nginx worker can be executed interactively via a JSON-RPC 2.0
interface.  The per-worker REPL relay lets operators target a specific worker by
index.

---

## Architecture

```
Browser (JavaScript SPA)
    │
    │ HTTP GET /admin/   → serve static SPA
    │ WS  GET /admin/ws  → upgrade to WebSocket
    │
nginx location (JS content handler)
    │
    │ req.hijack()  →  takes over the raw connection fd
    │
admin-shell.js (JS app running in worker N)
    │
    │ nginx.repl.eval(code)   →  evaluate in this worker
    │ via SharedWorker relay  →  evaluate in worker M (M ≠ N)
    │
    │ nginx.plugins.list()
    │ nginx.shared.*
    │ nginx.http.upstreams.*
```

---

## nginx.repl Object

Implemented in `ngx_js_repl.c`.  Exposed as `nginx.repl` on the global `nginx`
object.

### eval(line) → result object
```javascript
var r = nginx.repl.eval('nginx.http.servers.length');
// r = {status: 'ok', value: '1'}

var r = nginx.repl.eval('syntaxError(');
// r = {status: 'error', message: 'SyntaxError: …', stack: '…'}

var r = nginx.repl.eval('function f(');
// r = {status: 'incomplete'}   ← partial input, await more lines
```

The evaluator maintains a persistent context per `nginx.repl` instance so
variables defined in one eval are visible in subsequent evals.

`status: 'incomplete'` is returned when the QuickJS parser detects unterminated
syntax (open brace, unmatched string, etc.).  The admin shell buffers input until
the expression is complete.

### listen(fd, onLine)
Registers an nginx event-loop read handler on `fd`.  When a newline-terminated
line is available, calls `onLine(line)`.  Used for the REPL's stdin pipe.

### listenRaw(fd, onData)
Like `listen` but delivers raw `Uint8Array` chunks without line-splitting.
Used when the admin shell wants to receive binary frames directly (e.g., for a
binary-protocol tunnel or for the WebSocket frame parser).

### _writeFd(fd, str) / _writeFdRaw(fd, uint8array)
Write a string or binary buffer to a file descriptor.  These bypass any nginx
buffering and write directly.  Used by the admin shell to send WebSocket frames
back to the browser.

---

## Per-Worker REPL Relay (SharedWorker)

Each worker runs the same admin-shell code.  When the browser opens
`/admin/ws?w=2`, the WebSocket lands on whichever worker nginx assigns the
connection to.  If that worker is not worker 2, it must relay eval requests.

The relay uses a `SharedWorker` running in the master process:

```javascript
// repl-relay.js (SharedWorker script)
var slots = {};   // {workerIdx: port}

onconnect = function(e) {
    var port = e.ports[0];
    port.onmessage = function(msg) {
        if (msg.data.register !== undefined) {
            slots[msg.data.register] = port;
        } else if (msg.data.target !== undefined) {
            var target = slots[msg.data.target];
            if (target) target.postMessage({relay: msg.data.code, from: port});
        } else if (msg.data.result !== undefined) {
            msg.data.from.postMessage(msg.data.result);
        }
    };
};
```

Each worker registers itself in the SW on startup.  When a relay eval request
arrives for worker M, the SW forwards it to worker M's registered port and sends
the result back.

This design avoids any direct worker-to-worker communication path (there isn't
one in nginx) by routing through the stable SW thread.

---

## WebSocket Handshake (admin-shell.js)

The admin-shell JS app implements RFC 6455 entirely in JS (no native WebSocket
support in nginx):

1. `req.hijack()` returns the raw socket fd.
2. Read the HTTP Upgrade request headers.
3. Compute SHA-1 of `Sec-WebSocket-Key + magic UUID` → Base64 response.
4. Send `101 Switching Protocols` response.
5. Enter the frame loop: read frames, dispatch JSON-RPC, write frame responses.

SHA-1 and Base64 are implemented directly in `admin-shell.js` (~50 lines each)
to avoid any external dependency.

---

## JSON-RPC 2.0 Dispatch

The admin shell dispatches on the `method` field of each JSON-RPC request:

| Method | Handler |
|---|---|
| `plugins.list` | `nginx.plugins.map(...)` |
| `shared.get` | `nginx.shared.get(key)` |
| `shared.set` | `nginx.shared.set(key, val)` |
| `shared.delete` | `nginx.shared.delete(key)` |
| `shared.keys` | `nginx.shared.keys()` |
| `nginx.eval` | `nginx.repl.eval(code)` (possibly relayed) |
| `upstreams.list` | `nginx.http.upstreams.map(...)` |
| `upstreams.setPeer` | `ups.peers[i].weight = ...` |

Error responses follow JSON-RPC 2.0 error object format:
```json
{"jsonrpc": "2.0", "id": 42, "error": {"code": -32603, "message": "…"}}
```

---

## admin_UI_demo_v2 — Enhanced Admin App

Located in `js_pilgrim_apps/admin_UI_demo_v2/`.

Features beyond the basic admin shell:
- **nginx.* object tree browser**: renders the full COM tree as a collapsible
  tree widget; allows clicking any property to copy its path.
- **Per-worker REPL tabs**: tab strip `[W0] [W1] … [WN-1]`; each tab sends evals
  to the corresponding worker via the relay.
- **Plugin manager panel**: lists loaded plugins with their configs; has an
  "Install" button that calls `nginx.install(...)` live.
- **Upstreams panel**: shows peer health; allows toggling `.down` and adjusting
  `.weight` with instant effect.
- **Metrics plugin**: collects `nginx.http.servers[*].requests` counters and
  displays a live sparkline.

---

## Security Notes

- The admin WebSocket endpoint should be restricted by IP or behind auth middleware
  (e.g., the zero-trust-gateway demo app provides a JWT example).
- `nginx.repl.eval` executes arbitrary JS with full access to the `nginx.*` COM
  tree, including the ability to modify upstreams, load new plugins, and read
  `nginx.shared`.
- There is no sandboxing or permission model inside the REPL.  It is intended for
  trusted operators only.
