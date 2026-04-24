# B2.3 — HMAC Request Signing

## What it shows

Generates a signed `Authorization` header for outgoing upstream requests,
following the structural pattern of AWS Signature V4 and similar API signing
schemes:

1. Build a **canonical string**: `METHOD\nURI\nTimestamp`
2. Compute a **signature** from the canonical string + a secret key
3. Assemble an `Authorization: Hmac-SHA256 Credential=...,Signature=...` header

The demo echoes the would-be headers in the response so it is self-contained
(no real upstream required).

**Note on the hash function:** QuickJS ships without a native crypto library,
so this demo uses a deterministic polynomial rolling hash to illustrate the
concept.  In production the `pseudoHmac()` call would be replaced by a native
C binding that runs real HMAC-SHA256; the JavaScript surrounding it (canonical
string construction, header assembly, key-id lookup) remains unchanged.

## Classic nginx comparison

Classic nginx has no ability to sign outgoing requests.  The only mechanism is
`proxy_set_header` with a static string — dynamic, per-request cryptographic
headers require Lua (OpenResty) or an intermediate application.  With the JS
module the signing logic is a few lines of JavaScript executed inline.

## How to run

```bash
cd B2.3_HMAC_request_signing
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Sign a GET to /api/orders — shows the Authorization header that would be sent
curl http://localhost:8137/sign/?path=/api/orders

# Inspect response headers directly
curl -D - http://localhost:8137/sign/?path=/api/orders

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
