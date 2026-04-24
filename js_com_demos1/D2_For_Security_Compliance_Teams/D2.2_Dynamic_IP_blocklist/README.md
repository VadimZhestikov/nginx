# D2.2 — Dynamic IP Blocklist

## What this demo shows

A live IP blocklist stored in `nginx.shared` (nginx's cross-worker shared
memory store) that can be updated through a simple admin API. A location hook
on `/api/` checks every inbound request's `X-Real-IP` header against the store
in a single `nginx.shared.get()` call — no external lookup, no SharedWorker,
latency under 2 µs.

Key mechanics:
- `nginx.shared.set('block:10.0.0.99', '1')` — add an IP
- `nginx.shared.delete('block:10.0.0.99')` — remove an IP
- `nginx.shared.get('block:10.0.0.99')` in the hook — check (falsy = allowed)
- `nginx.shared.keys()` — enumerate all blocked IPs
- Changes take effect immediately for ALL workers without any reload

## Why it is powerful

Traditional nginx IP blocking is static:

```nginx
geo $blocked { default 0; 10.0.0.99 1; }
```

Updating the list requires editing `nginx.conf` and `nginx -s reload`. With
`nginx.shared` the blocklist is a live data structure that any admin API call
can modify. The hook check is a single hash-map lookup — faster than a
`geo` module linear scan for large lists.

## Classic nginx approach

```nginx
# static — requires reload to update
geo $blocked {
    default       0;
    10.0.0.99     1;
}
location /api/ {
    if ($blocked) { return 403; }
    proxy_pass http://backend;
}
```

## Key API

```javascript
// Add an IP to the blocklist (visible to all workers immediately)
nginx.shared.set('block:' + ip, '1');

// Check in a hook
loc.addHook(function(r) {
    var ip = r.headers['x-real-ip'];
    if (nginx.shared.get('block:' + ip)) {
        r.respond(403, {}, 'Forbidden\n');
        return;  // stop the chain — content handler skipped
    }
});

// Enumerate
nginx.shared.keys()
    .filter(k => k.startsWith('block:'))
    .map(k => k.slice('block:'.length));
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Clean IP — passes through
curl -H "X-Real-IP: 10.0.0.1" http://127.0.0.1:8181/api/
# → OK — request from 10.0.0.1

# Block an IP (instant — no reload)
curl -X POST -H "Content-Type: application/json" \
     -d '{"ip":"10.0.0.99"}' \
     http://127.0.0.1:8181/admin/block/
# → {"blocked":"10.0.0.99"}

# Blocked IP is immediately rejected
curl -i -H "X-Real-IP: 10.0.0.99" http://127.0.0.1:8181/api/
# → HTTP/1.1 403 Forbidden

# Clean IP still passes
curl -H "X-Real-IP: 10.0.0.1" http://127.0.0.1:8181/api/
# → OK — request from 10.0.0.1

# Inspect the blocklist
curl http://127.0.0.1:8181/admin/blocklist/
# → {"count":1,"blocked_ips":["10.0.0.99"]}

# Unblock
curl -X POST -H "Content-Type: application/json" \
     -d '{"ip":"10.0.0.99"}' \
     http://127.0.0.1:8181/admin/unblock/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

In production, `X-Real-IP` would be set by the `ngx_http_realip_module` using
the actual TCP source address. CIDR block matching can be added by parsing the
IP into an integer and comparing against stored ranges — all in plain
JavaScript.
