# B4.3 — DNS-Refreshed Upstream Addresses

## What it shows

A **SharedWorker** daemon periodically re-resolves upstream hostnames and stores
the latest IP addresses.  The `/dns-status/` endpoint shows the current
resolution; `/admin/refresh/` triggers an immediate re-resolution cycle (cycling
to the next set of demo IPs).

In a real deployment the SharedWorker would call `os.exec(['getent', 'hosts',
hostname])` or open a raw UDP socket to a nameserver on a `setInterval` timer,
automatically tracking short-TTL DNS changes without any nginx reload.

## Classic nginx comparison

Upstream server addresses in open-source nginx are resolved **once** at startup
or reload.  Automatic re-resolution of DNS changes (short-TTL records, blue/green
deployments, cloud load-balancer IP rotation) is only supported in nginx Plus via
the `resolver` directive + `resolve` upstream parameter.

With the JS SharedWorker, the refresh logic is a few lines of JavaScript and
runs inside the nginx process at whatever interval you choose — no nginx Plus
required.

## How to run

```bash
cd B4.3_DNS_refreshed_upstream
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# View current resolved IPs (initial cycle)
curl http://localhost:8147/dns-status/

# Trigger a re-resolution — IPs cycle to next set
curl -X POST http://localhost:8147/admin/refresh/

# View updated IPs
curl http://localhost:8147/dns-status/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
