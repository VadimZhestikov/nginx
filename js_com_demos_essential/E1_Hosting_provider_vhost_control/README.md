# E1 — Hosting-provider vhost control

## Scenario

You run a shared nginx instance serving thousands of customer sites. When a new
customer signs up you need to bring their site online; when they stop paying you
need to take it offline — all instantly, without touching nginx.conf or
reloading. A conventional `nginx -s reload` adds latency, requires file-system
access, and briefly runs two nginx processes in parallel. At scale it becomes
a bottleneck.

## What this demo shows

`nginx.http.addServer(hostname)` creates a live virtual server identified by
its `Host:` header. Calling `http.rebuildVhostDispatch()` makes it reachable
within the same request. `http.removeServer(hostname)` removes it with the
same one-call activation. All changes are isolated to the running process —
no config file, no reload signal.

## Key API

```javascript
// Enable a new tenant site
var srv = nginx.http.addServer('alice.example.com');
srv.addLocation('/').handler = function(r) {
    r.respond(200, {}, 'Welcome, Alice\n');
};
srv.addLocation('/health').handler = function(r) {
    r.respond(200, {}, 'ok\n');
};
nginx.http.rebuildVhostDispatch();   // makes it live instantly

// Disable it
nginx.http.removeServer('alice.example.com');
nginx.http.rebuildVhostDispatch();
```

## Architecture

```
port 8202 (api server, Host: api.example.com)     ← public traffic
port 8203 (ops server, Host: ops.internal)         ← internal control plane
```

Two static servers on port 8201 are required so nginx builds a vhost-dispatch
hash at start-up that `addServer` / `removeServer` can extend at runtime.

## Control-plane API

| Method | Path | Action |
|--------|------|--------|
| `POST` | `/tenants/enable?<hostname>` | Bring tenant site online |
| `POST` | `/tenants/disable?<hostname>` | Take tenant site offline |
| `GET`  | `/tenants/list` | List active tenant hostnames |

## How to run

```bash
bash test.sh
```

## Manual walkthrough

```bash
# Start nginx
../../objs/nginx -p . -c nginx.conf

# List tenants (empty at start)
curl http://127.0.0.1:8201/tenants/list
# → (no active tenants)

# Enable alice
curl -X POST "http://127.0.0.1:8201/tenants/enable?alice.example.com"
# → enabled: alice.example.com

# Alice's site is live — no reload
curl -H "Host: alice.example.com" http://127.0.0.1:8201/
# → Welcome to alice.example.com

# Enable bob
curl -X POST "http://127.0.0.1:8201/tenants/enable?bob.example.com"

# List active tenants
curl http://127.0.0.1:8201/tenants/list
# → alice.example.com
# → bob.example.com

# Suspend alice
curl -X POST "http://127.0.0.1:8201/tenants/disable?alice.example.com"
# → disabled: alice.example.com

# Alice is gone; bob is unaffected
curl -H "Host: alice.example.com" http://127.0.0.1:8201/
# → 404
curl -H "Host: bob.example.com" http://127.0.0.1:8201/
# → Welcome to bob.example.com

# Stop nginx
../../objs/nginx -p . -c nginx.conf -s stop
```
