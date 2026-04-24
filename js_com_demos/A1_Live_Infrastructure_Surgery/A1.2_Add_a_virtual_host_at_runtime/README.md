# A1.2 — Add a Virtual Host at Runtime

## What this demo shows

`nginx.http.addServer(name)` creates a new virtual server identified by its
`server_name`. After calling `http.rebuildVhostDispatch()` the new server is
live: requests with matching `Host:` headers are routed to it. The new server
supports the full NGINX JS COM API — you can add locations, install handlers,
attach hooks and body filters — all without touching nginx.conf or reloading.

## Classic nginx approach

Adding a virtual host requires editing `nginx.conf` and issuing `nginx -s reload`
(or a full restart in older setups). Reload requires file system access, config
validation, and a brief dual-process window. In automated tenant-onboarding
flows this creates a bottleneck.

## Key API

```javascript
// Create a new virtual server
var srv = nginx.http.addServer('tenant42.example.com');

// Install handlers on new locations
srv.addLocation('/api/').handler = function(r) {
    r.respond(200, {}, 'tenant42 API\n');
};

// Activate in the vhost dispatch table (must call this after mutations)
nginx.http.rebuildVhostDispatch();

// Later: remove it
nginx.http.removeServer('tenant42.example.com');
nginx.http.rebuildVhostDispatch();
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx (two static servers on port 8102)
../../../objs/nginx -p . -c nginx.conf

# Static server 2 is already alive
curl -H "Host: static2.local" http://127.0.0.1:8102/ping/
# → static2-pong

# Dynamic host does not exist yet — falls through to 404
curl -H "Host: dynamic.host" http://127.0.0.1:8102/api/
# → 404

# Create the virtual host at runtime
curl -X POST "http://127.0.0.1:8102/add/?dynamic.host"
# → created: dynamic.host

# Now reachable via Host header — no reload happened
curl -H "Host: dynamic.host" http://127.0.0.1:8102/api/
# → Hello from dynamic server: dynamic.host

# List all servers
curl http://127.0.0.1:8102/list/
# → static1.local
# → static2.local
# → dynamic.host

# Remove at runtime
curl -X POST "http://127.0.0.1:8102/remove/?dynamic.host"
# → removed: dynamic.host

# Gone again
curl -H "Host: dynamic.host" http://127.0.0.1:8102/api/
# → 404

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

Two static servers on the same listen address are required so that nginx
builds a vhost-dispatch hash at config time. `addServer()` extends that hash;
`rebuildVhostDispatch()` makes the extension visible to the request router.
