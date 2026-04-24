# A4.3 — COM Object Tree Browser

## What this demo shows

A browser-based graphical tree view of the entire nginx COM object hierarchy:

```
nginx
├── version: "1.27.x"
├── cpu_count: 4
├── workerIdx: 0
├── http
│   ├── servers[]
│   │   ├── [0] app.local :8080
│   │   │   ├── locations[]
│   │   │   │   ├── [0] /api/v1/
│   │   │   │   └── [1] /static/
│   │   │   └── ssl: { ... }
│   │   └── [1] api.local :8081
│   └── upstreams[]
│       └── [0] backend { peers: [{weight:1}, {weight:2}] }
└── shared: { keys: ["feature.flag", "max.rps"] }
```

Clicking a node shows its properties; clicking a property with a setter
allows editing the value inline. Changes take effect immediately.

## Classic nginx approach

Classic nginx has no introspection API. The configuration is static text that
must be read from the filesystem. Runtime state (active connections, upstream
health) is available only via `stub_status` module in aggregate form.

## How to run

This demo requires the admin UI application in `js_com_apps/`. See the admin
UI README for setup instructions.

## What you can explore

| COM path | What it shows |
|----------|--------------|
| `nginx.http.servers[n]` | Server config, locations, SSL state |
| `nginx.http.servers[n].locations[m]` | Handler, hooks, proxy/gzip/headers settings |
| `nginx.http.upstreams[n].peers` | Weight, max_fails, fail_timeout per peer |
| `nginx.shared` | Cross-worker KV store keys and values |
| `nginx.workerIdx` | Which worker is currently responding |

## Key concept

```javascript
// The tree browser walks the COM object tree recursively:
function serializeServer(s) {
    return {
        name:      s.name,
        locations: s.locations.map(l => ({
            path:    l.path,
            pattern: l.pattern,
            hasHandler: typeof l.handler === 'function',
        })),
    };
}

// All readable from JS — no nginx.conf parsing required
var tree = nginx.http.servers.map(serializeServer);
```
