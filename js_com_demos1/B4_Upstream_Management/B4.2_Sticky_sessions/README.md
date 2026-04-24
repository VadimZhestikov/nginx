# B4.2 — Sticky Sessions (Hash-based upstream routing)

## What it shows

Routes each request to a **deterministic backend** by hashing the
`X-Session-Token` header.  The same token always maps to the same backend —
providing session stickiness without a shared session store or a sticky-session
cookie.

Fallback order:
1. `X-Session-Token` header
2. `Authorization: Bearer <token>` header (uses raw token as the hash key)
3. Default to backend A

The hash function (djb2) is transparent JavaScript; the routing decision is
a plain array index — no black-box upstream module required.

## Classic nginx comparison

Classic nginx supports `hash $variable consistent;` inside an upstream block
for consistent hashing.  However:
- The result is not inspectable — you cannot log which backend was chosen
- The fallback key sequence requires multiple `map {}` blocks
- The hash function cannot be customised

With the JS module the hash is explicit, the backend selection is visible in
the response headers (`X-Backend`), and the fallback logic is a plain `if/else`.

## How to run

```bash
cd B4.2_Sticky_sessions
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Same token always lands on the same backend
for i in 1 2 3; do
    curl -s -H "X-Session-Token: user-abc-token" http://localhost:8144/sticky/
done

# A different token may land on a different backend
curl -s -H "X-Session-Token: user-xyz-token" http://localhost:8144/sticky/

# Show the X-Backend header
curl -sD - -H "X-Session-Token: user-abc-token" http://localhost:8144/sticky/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
