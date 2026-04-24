# D3.2 — Single Pane of Glass Admin UI

## What this demo shows

A browser-based admin dashboard that talks to the nginx JS COM API via a
thin HTTP JSON bridge, providing real-time visibility and control over a
running nginx instance:

- Live server/location inventory (`nginx.http.servers[]`)
- Add / remove virtual hosts without reloading
- Toggle location handlers and hooks
- View upstream peer weights and health
- Cross-worker metrics via `nginx.shared`

This demo requires the separate **admin_ui_demo_v2** application that is
checked into this repository. The minimal placeholder files in this directory
confirm the test harness and nginx config are wired up correctly.

## Where to find the full UI

The full admin UI is the `admin_ui_demo_v2` application at the root of the
`js_com_demos1` directory. To run it:

```bash
# (from repo root)
cd admin_ui_demo_v2
bash test.sh
# Then open http://127.0.0.1:8099/ in a browser
```

## Architecture

```
Browser ──HTTP──► nginx (JS COM bridge)
                    │
                    ├── nginx.http.servers[]
                    ├── nginx.http.addServer()
                    ├── nginx.shared.get/set()
                    └── nginx.http.upstreams[]
```

The JS COM bridge is a set of `/admin/*` locations that serialise nginx
internal state to JSON and accept mutation commands via POST. The browser
UI is a single-page application served from `/ui/` that calls these endpoints.

## Why it is powerful

Classic nginx has no built-in management API. Admin operations require:
- Shell access to the server to edit files
- `nginx -s reload` with its dual-process window
- Third-party tools (nginx-proxy-manager, Kubernetes ingress controllers)
  that all reinvent config management

With the JS COM API the full nginx configuration graph is an in-process
JavaScript object tree. Any browser-accessible HTTP endpoint can read
and mutate it — making nginx self-administering without any external tooling.

## How to run the minimal placeholder

```bash
bash test.sh
```

This confirms nginx starts correctly. The full admin UI is in admin_ui_demo_v2.
