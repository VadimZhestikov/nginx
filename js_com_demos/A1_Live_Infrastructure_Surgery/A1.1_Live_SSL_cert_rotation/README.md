# A1.1 — Live SSL Certificate Rotation

## What this demo shows

NGINX JS COM exposes `server.ssl.setCertificate(certPEM, keyPEM)` which swaps
the TLS certificate and private key in memory **without reloading or restarting
nginx**. New TLS handshakes immediately use the new certificate; in-flight
connections keep their original cert. This enables:

- Automated Let's Encrypt / ACME certificate renewal with zero downtime
- Emergency certificate replacement after a private key compromise
- Canary cert rollouts (swap for a subset of servers only)

## Classic nginx approach

Classic nginx requires `nginx -s reload` to pick up a new certificate. During
reload, a new master process parses config and starts new workers — there is a
brief window where both old and new workers coexist, and in-flight requests may
see either cert. In containerised environments reload can be complex.

## How it works (production setup)

```nginx
server {
    listen 443 ssl;
    ssl_certificate     /etc/nginx/certs/current.crt;
    ssl_certificate_key /etc/nginx/certs/current.key;
    server_name secure.example.com;
    ...
}
```

```javascript
// In handler.js — called from an admin webhook or ACME callback:
var sslServer = nginx.http.servers.find(s => s.name === 'secure.example.com');

// certPEM and keyPEM are PEM strings from your CA or ACME client:
sslServer.ssl.setCertificate(certPEM, keyPEM);

nginx.log(4, 'TLS cert rotated — new expiry: ' + parsedExpiry);
```

## This demo

Since generating a valid self-signed cert requires `openssl` tooling, this demo
runs an HTTP server on port **8101** and simulates the rotation workflow:

- `GET /status/` — returns the currently "active" cert version label
- `POST /admin/cert/` with `X-Cert-Version: <label>` — simulates calling
  `setCertificate()` and records the new version

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Show initial state
curl http://127.0.0.1:8101/status/
# → active-cert: v1-initial

# Rotate certificate (simulated)
curl -X POST -H "X-Cert-Version: v2-lets-encrypt" http://127.0.0.1:8101/admin/cert/
# → Certificate rotated to v2-lets-encrypt

# Confirm new cert is active — no reload was needed
curl http://127.0.0.1:8101/status/
# → active-cert: v2-lets-encrypt

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Key API

```javascript
server.ssl.setCertificate(certPEM, keyPEM);
```

`certPEM` and `keyPEM` are PEM-encoded strings. The call is synchronous and
takes effect for all subsequent TLS handshakes within the current worker.
