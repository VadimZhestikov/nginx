# B3.3 — Response Fan-out / Fan-in

## What it shows

A single `/product/?id=N` request triggers **three internal subrequests** — to
simulated price, stock, and rating microservices — then merges all three JSON
responses into one unified product object returned to the client.

```
GET /product/?id=5
    │
    ├─► subrequest /internal/price/?id=5   → {"price":32.49,"currency":"USD"}
    ├─► subrequest /internal/stock/?id=5   → {"inStock":true,"quantity":85}
    └─► subrequest /internal/rating/?id=5  → {"rating":4.5,"reviews":163}
            │
            ▼
    {"id":5,"price":{...},"stock":{...},"rating":{...}}
```

All processing happens inside a single nginx worker event loop — no external
process, no extra network hop.

## Classic nginx comparison

Classic nginx has no mechanism for fan-out/fan-in.  `ngx_http_mirror_module`
replicates requests to an alternate backend but does not collect and merge
responses.  Achieving aggregation requires a BFF (Backend-for-Frontend) service,
a GraphQL gateway, or OpenResty with `ngx.location.capture_multi`.  With the JS
module, `await r.subrequest()` and plain `JSON.parse` / object spread do the job
in under 20 lines of JavaScript.

## How to run

```bash
cd B3.3_Response_fan_out_fan_in
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Fetch aggregated product (id=1)
curl http://localhost:8140/product/?id=1

# Try another product id
curl http://localhost:8140/product/?id=7

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
