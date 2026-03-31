# admin_UI_demo

A self-contained demonstration of the **admin-shell** package (P19).
Shows the admin shell UI working against a realistic nginx config with
upstream pools, live request counters, shared state, and two sample plugins.

## Quick start

Run from the **repository root**:

```bash
objs/nginx -p . -c js_pilgrim_apps/admin_UI_demo/nginx.conf
```

Then open:

| URL | Description |
|-----|-------------|
| http://localhost:8080/ | Demo landing page |
| http://localhost:8080/admin/ | Admin Shell UI |
| http://localhost:8080/info | Runtime JSON |
| http://localhost:8080/health | Health check |

Stop with:

```bash
objs/nginx -p . -c js_pilgrim_apps/admin_UI_demo/nginx.conf -s stop
```

## What's loaded

`init.js` loads three plugins in order:

| Plugin | Path | What it does |
|--------|------|-------------|
| admin-shell | `js_pilgrim_apps/admin-shell` | WebSocket JSON-RPC server on `/admin/ws`; serves the browser UI |
| metrics | `plugins/metrics` | Increments `metrics:<path>:requests` counters in `nginx.shared` on every request |
| banner | `plugins/banner` | Adds `X-Powered-By` and `X-Request-Id` headers to every response |

## Demo features

### Landing page (`/`)
Connects to the admin WebSocket and displays:
- Loaded plugin list (from `nginx.plugins`)
- Live request counters (auto-refreshes every 5 s)
- Upstream pool topology

### Admin Shell (`/admin/`)
Full admin UI with four panels:

| Panel | Try this |
|-------|---------|
| **Plugins** | See all three loaded plugin paths |
| **Shared State** | Edit `config:max_rps` or toggle `feature:beta_ui` |
| **Eval** | Run `nginx.shared.keys()` or `nginx.plugins.length` |
| **Upstreams** | See web_backend / api_backend / cache_backend peer weights |

### Shared state seeded by `init.js`

| Key | Initial value |
|-----|---------------|
| `demo:app` | `admin_UI_demo` |
| `demo:version` | `1.0.0` |
| `demo:environment` | `development` |
| `demo:started_at` | ISO timestamp |
| `demo:workers` | CPU count |
| `config:log_level` | `notice` |
| `config:max_rps` | `1000` |
| `feature:beta_ui` | `false` |
| `feature:tracing` | `false` |

## Sample upstream pools

The config declares three pools (backends don't need to be running
for the admin UI to work):

```
web_backend   127.0.0.1:8091 w=5
              127.0.0.1:8092 w=3
              127.0.0.1:8093 w=2 (backup)

api_backend   127.0.0.1:8094 w=10
              127.0.0.1:8095 w=5

cache_backend 127.0.0.1:8096 w=1
```
