# B5.1 — Request Tracing (X-Request-Id propagation)

## What it shows

Ensures every request through nginx carries a trace ID:

- If the client sends `X-Request-Id`, that exact value is **preserved** and
  echoed back in the response (distributed tracing style).
- If no ID is present, a new **pseudo-UUID** is generated and attached to the
  request context, response headers, and response body.

Two JS hooks collaborate:
- `addHook` (request phase) — assigns `r.ctx.requestId` from incoming header
  or generates a new one.
- `addResponseHook` (response phase) — writes the id via `r.setHeader('x-request-id', ...)` .

## Classic nginx comparison

`$request_id` (nginx 1.11.0+) generates a random lowercase hex string.  It
**cannot** reuse an existing `X-Request-Id` from the client — every request
gets a fresh id, breaking distributed trace continuity.  Setting the response
header with `add_header` has well-known inheritance quirks when location blocks
are nested.  The JS hooks give explicit, predictable control over both the
propagation logic and the header assignment.

## How to run

```bash
cd B5.1_Request_tracing
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Auto-generated id (check response headers)
curl -D - http://localhost:8148/api/

# Client-supplied id is preserved
curl -D - -H "X-Request-Id: my-trace-12345" http://localhost:8148/api/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
