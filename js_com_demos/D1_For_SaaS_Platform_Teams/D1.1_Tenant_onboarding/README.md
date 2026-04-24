# D1.1 — Tenant Onboarding

## What this demo shows

A SaaS platform admin API that provisions a complete new tenant virtual host
in a single HTTP call. A `POST /admin/tenants/` with `{"name":"acme-corp"}`
creates a new nginx virtual server with its own `/api/` and `/health/`
handlers, live within milliseconds — no config file edit, no reload, no
service interruption for existing tenants.

Key mechanics:
- `nginx.http.addServer(name)` creates a new virtual server identified by
  `server_name`
- `srv.addLocation(path).handler = fn` wires up per-tenant request handlers
- `nginx.http.rebuildVhostDispatch()` publishes the new server to the request
  router atomically
- `GET /status/` reads `nginx.http.servers[]` to show live inventory

## Why it is powerful

Tenant onboarding in traditional nginx requires:
1. Editing `nginx.conf` or a conf.d snippet file
2. Running `nginx -s reload` (validates config, forks a new worker generation)
3. Waiting for the old workers to drain in-flight requests

That pipeline adds several hundred milliseconds of latency per onboarding and
requires file-system write access from the provisioning service. With the JS
COM API the same operation is a single in-process function call that completes
in under 1 ms with zero file I/O and no worker restart.

## Classic nginx approach

```nginx
# Must write a file like /etc/nginx/conf.d/acme-corp.conf
server {
    server_name  acme-corp;
    location /api/ { proxy_pass http://acme-backend; }
}
# Then: nginx -s reload
```

## Key API

```javascript
// Create the virtual host
var srv = nginx.http.addServer('acme-corp');

// Attach tenant-specific handlers
srv.addLocation('/api/').handler = function(r) {
    r.respond(200, {}, 'acme-corp API\n');
};

// Make it live — single atomic operation
nginx.http.rebuildVhostDispatch();

// Inspect live server inventory
nginx.http.servers.map(s => s.name);
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Check initial status — 2 servers (admin + anchor)
curl http://127.0.0.1:8175/status/

# Provision "acme-corp" tenant (< 100 ms round-trip)
curl -X POST -H "Content-Type: application/json" \
     -d '{"name":"acme-corp"}' \
     http://127.0.0.1:8175/admin/tenants/
# → {"status":"created","tenant":"acme-corp","api":"..."}

# Immediately access the new tenant — no reload happened
curl -H "Host: acme-corp" http://127.0.0.1:8175/api/
# → {"tenant":"acme-corp","message":"Welcome to acme-corp API","status":"active"}

# Provision a second tenant
curl -X POST -H "Content-Type: application/json" \
     -d '{"name":"beta-inc"}' \
     http://127.0.0.1:8175/admin/tenants/

# Both tenants live simultaneously
curl -H "Host: beta-inc" http://127.0.0.1:8175/api/
# → {"tenant":"beta-inc",...}

# Status shows all three servers
curl http://127.0.0.1:8175/status/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

Two static servers (`admin.local` and `anchor.local`) must be declared on
the same listen address so nginx builds a vhost-dispatch hash at startup.
`addServer()` extends that hash; `rebuildVhostDispatch()` atomically publishes
the update to the request router without restarting any workers.
