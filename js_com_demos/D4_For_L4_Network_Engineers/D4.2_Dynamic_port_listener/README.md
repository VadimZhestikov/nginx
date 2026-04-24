# D4.2 — Dynamic Port Listener

## What this demo shows

Dynamic provisioning of network services via an admin API, with services
immediately accessible without config file edits or nginx reload. Each `POST
/admin/listen/` call creates a new virtual host with its own `/data/` and
`/health/` endpoints, tracked in `nginx.shared` for cross-worker visibility.

This demo uses `nginx.http.addServer()` (virtual host isolation per hostname)
to demonstrate the provisioning pattern. The README explains how a full
implementation would extend this to port-level isolation using
`nginx.createSocket()`.

## Production concept: `nginx.createSocket(port)`

A complete L4 dynamic port listener would:

```javascript
// POST /admin/listen/ → allocate a new TCP listener at runtime
var sock = nginx.createSocket({
    port: data.port || allocatePort(),
    protocol: 'tcp'
});

sock.onconnect = function(conn) {
    // Handle each new TCP connection in JS
    conn.pipe(upstreamPool.next());
};
```

This enables use cases like:
- Gaming servers: each game session gets a unique UDP/TCP port
- Media streaming: each WebRTC session gets a dedicated TURN relay port
- Multi-tenant services: per-tenant port allocation without firewall changes

## Why it is powerful

Traditional nginx requires a static listen directive for every port. Adding a
new listener requires editing `nginx.conf` and issuing `nginx -s reload`.
For dynamic services (game sessions, media streams) that come and go in
seconds, this reload-per-port model is unworkable.

With a programmatic socket API the listener lifecycle is driven by application
logic — same as a Node.js or Go server, but within the nginx event loop with
all its performance and reliability characteristics.

## Classic nginx approach

```nginx
# Static — requires a reload for every new port
server { listen 8201; location / { proxy_pass http://svc-alpha; } }
server { listen 8202; location / { proxy_pass http://svc-beta; } }
# ... one block per service
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Initial state — no services
curl http://127.0.0.1:8191/status/
# → {"registered_services":0,...}

# Provision a game server service
curl -X POST -H "Content-Type: application/json" \
     -d '{"name":"svc-alpha","service":"game-server"}' \
     http://127.0.0.1:8191/admin/listen/
# → {"status":"registered","service":"svc-alpha","type":"game-server",...}

# Access via Host header (production: via dedicated port)
curl -H "Host: svc-alpha" http://127.0.0.1:8191/data/
# → {"service":"svc-alpha","type":"game-server","status":"active",...}

# Readiness probe
curl -H "Host: svc-alpha" http://127.0.0.1:8191/health/
# → ok service=svc-alpha

# Provision second service
curl -X POST -H "Content-Type: application/json" \
     -d '{"name":"svc-beta","service":"video-relay"}' \
     http://127.0.0.1:8191/admin/listen/

# Status shows both services
curl http://127.0.0.1:8191/status/
# → {"registered_services":2,"services":[...],...}

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
