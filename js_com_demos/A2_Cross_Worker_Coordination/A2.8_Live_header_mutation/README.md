# A2.8 — Live Header Mutation — Two Approaches Side by Side

Demonstrates two complementary ways to change HTTP response headers at runtime
without editing `nginx.conf` and without reloading nginx.  A persistent WebSocket
in the browser shows both approaches updating live across all 4 workers.

## What it shows

### Part 1 — Request-phase header via `nginx.shared`

| Primitive | Role |
|---|---|
| `nginx.shared` | Lock-free KV shared across all workers — one write, all workers see it instantly |
| `nginx.broadcast(fn)` | Installs the same JS handler in every worker at startup |
| `req.hijack()` + `nginx.repl.listenRaw` | Persistent WebSocket connection; browser polls every 300 ms |

The `X-Api-Header` value lives in `nginx.shared`.  An admin POST changes it; the
browser WebSocket connection — which is never closed — reflects the new value in
< 300 ms.  All 4 workers serve the new header on their very next request.

### Part 2 — Config-phase `add_header` mutation via `loc.headers` API

| Primitive | Role |
|---|---|
| `loc.headers.addHeader(key, value[, always])` | Appends one runtime `add_header` entry to a location |
| `loc.headers.removeHeader(key)` | Removes entries by key (case-insensitive) |
| `loc.headers.addHeaders = [...]` | Replaces the entire `add_header` list |
| `cfgbus.js` (SharedWorker) | Broadcast bus — fans each mutation to all 4 worker processes |

The `/plain/` location has **no JS content handler** — it is a pure nginx
`return 200` location.  The `add_header` config for that location is mutated at
runtime through the COM API.  A `cfgbus.js` SharedWorker fans every mutation to
all workers so `/plain/` responses carry the injected header from every worker
within ~1 ms.

Classic nginx requires `nginx -s reload` to add or remove `add_header` entries.
Here, one API call is sufficient — no reload, no dropped connections, no JS
handler needed on the target location.

## File layout

```
nginx.conf      4-worker server on :8117 (reuseport)
handler.js      js_source: Part 1 + Part 2 backend, two-panel browser UI
cfgbus.js       SharedWorker broadcast bus for Part 2 config mutations
test.sh         Self-contained test (starts nginx, runs 193 checks, stops nginx)
```

## Running

```bash
cd js_com_demos/A2_Cross_Worker_Coordination/A2.8_Live_header_mutation
../../../objs/nginx -p . -c nginx.conf
# open http://127.0.0.1:8117/ in a browser
```

To run the automated test suite (pre-flight before a live demo):

```bash
bash test.sh
```

## Browser UI

Opening `http://127.0.0.1:8117/` loads a two-panel page:

**Top panel — Part 1**
- Shows `X-Api-Header` current value (server-side injected at page load, then
  updated live via WebSocket polling)
- Input field + preset buttons to change the value via the admin endpoint
- Change log with timestamps and worker IDs

**Bottom panel — Part 2**
- Shows current `add_header` entries on `/plain/` (the no-JS-handler location)
- Form to inject a new header (`key`, `value`, `always` checkbox)
- **Remove** button per entry, **Clear All** button
- Live preview of actual HTTP response headers from `/plain/`

## Endpoints

### Part 1 — `nginx.shared` header

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Browser UI (HTML served inline by JS) |
| `GET` | `/ws/` | WebSocket upgrade — polls for `{type:"state"}`, accepts `{type:"set",value:"…"}` |
| `GET` | `/api/` | Returns `X-Api-Header` in response headers and JSON body |
| `GET` | `/status/` | Current shared state as JSON |
| `POST` | `/admin/set-header/?value=<val>` | Change `X-Api-Header` for all workers instantly |

### Part 2 — config-phase `add_header`

| Method | Path | Description |
|---|---|---|
| `POST` | `/admin/add-config-header/?key=K&value=V[&always=1]` | `addHeader` on `/plain/`, fan-out via cfgbus |
| `POST` | `/admin/remove-config-header/?key=K` | `removeHeader` on `/plain/`, fan-out via cfgbus |
| `POST` | `/admin/clear-config-headers/` | `addHeaders = []` on `/plain/`, fan-out via cfgbus |
| `GET` | `/admin/get-config-headers/` | Current `addHeaders` list + responding worker PID |
| `GET` | `/plain/` | Pure nginx `return 200` — no JS handler; carries injected headers |

## How it works

### Part 1

```
nginx.shared.set('demo.header', 'v2-premium')
        │
        ├─▶ worker 1: next nginx.shared.get() → "v2-premium"  (no IPC)
        ├─▶ worker 2: next nginx.shared.get() → "v2-premium"
        ├─▶ worker 3: next nginx.shared.get() → "v2-premium"
        └─▶ worker 4: next nginx.shared.get() → "v2-premium"

Browser WebSocket polls every 300 ms → receives {type:"state", header:"v2-premium"}
→ updates <div id="hdr-val"> in < 300 ms
```

### Part 2

```
POST /admin/add-config-header/?key=X-Feature&value=beta
        │
        ▼  (worker that received the request)
   plainLoc.headers.addHeader('X-Feature','beta')   ← local, immediate
   cfgBus.postMessage({type:'addHeader',...})
        │
        ▼  (cfgbus.js SharedWorker thread in master process)
   _current.push({key:'X-Feature',...})
   fan-out to workers B, C, D  (skip sender)
        │
        ├─▶ worker B onmessage → plainLoc.headers.addHeader(...)
        ├─▶ worker C onmessage → plainLoc.headers.addHeader(...)
        └─▶ worker D onmessage → plainLoc.headers.addHeader(...)

Result: /plain/ returns X-Feature: beta from all 4 workers within ~1 ms
```

The sender applies locally first (immediate); `cfgbus.js` fans out to all other
ports using `_ports[i] !== port` so the sender does not receive its own message
back.  When a worker restarts after a SIGHUP reload, it connects to the existing
`cfgbus.js` thread and receives `{type:'setState', headers:[…]}` to catch up with
the current state.

## cfgBus SharedWorker API pattern

`cfgbus.js` uses the `onconnect` / per-port pattern required by the nginx JS
SharedWorker implementation:

```js
var _ports = [], _current = [];

onconnect = function(e) {
    var port = e.ports[0];
    _ports.push(port);
    // Catch-up: send current state to the new worker
    port.postMessage({type: 'setState', headers: _current.slice()});
    port.onmessage = function(ev) {
        applyToState(ev.data);
        // Fan out to every OTHER port — the sender applied locally already
        for (var i = 0; i < _ports.length; i++) {
            if (_ports[i] !== port) { _ports[i].postMessage(ev.data); }
        }
    };
};
```

## Comparison

| | Classic nginx | Part 1 | Part 2 |
|---|---|---|---|
| Change takes effect after | ~500 ms (reload + worker respawn) | < 300 ms | < 1 ms |
| In-flight connections dropped | Yes (graceful drain) | No | No |
| nginx -s reload required | Yes | No | No |
| JS content handler on target location | — | Required | **Not required** |
| Works on `proxy_pass`, static, `return` | (config only) | No | **Yes** |
| Header value can be per-request dynamic | — | Yes | Literal only |
| All workers updated | — | Yes (shared memory) | Yes (cfgbus broadcast) |
