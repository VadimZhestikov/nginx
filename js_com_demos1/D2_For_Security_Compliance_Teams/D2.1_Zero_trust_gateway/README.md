# D2.1 — Zero Trust Gateway

## What this demo shows

Every request to `/api/*` is validated in-process against a Bearer token
before the content handler executes. The validation — token decoding, expiry
check, and role authorization — runs entirely inside the nginx JS engine with
no external auth service call and no sidecar proxy.

Key mechanics:
- `loc.addHook(fn)` attaches a pre-handler access phase check; if the hook
  calls `req.respond()`, the content handler is skipped
- Token claims are stashed in `req.ctx.claims` so the content handler can read
  the authenticated identity without re-decoding
- `/health/` has no hook — it is always reachable (liveness probe)
- Role-based access control: `/api/admin/` requires `role === 'admin'`
- Token expiry is enforced per request — no cache poisoning

## Why it is powerful

The traditional approach requires a separate `auth_request` subrequest to an
external auth microservice, adding a network round-trip to every API call.
With JS hooks the same logic runs in-process at ~microsecond latency, with
full access to request headers, URI, and per-request scratch space (`req.ctx`).
The validation logic is plain JavaScript — testable, versionable, and
changeable at runtime without rebuilding nginx.

## Classic nginx approach

```nginx
location /api/ {
    auth_request /auth-service/;   # external HTTP call on every request
    auth_request_set $user $upstream_http_x_user;
    proxy_pass http://backend;
}
location /auth-service/ {
    proxy_pass http://auth-sidecar:8080/validate;
}
```

## Key API

```javascript
// Attach an access-phase hook to a location
loc.addHook(function(req) {
    var result = validateToken(req.headers['authorization']);
    if (!result.ok) {
        req.respond(401, {'WWW-Authenticate': 'Bearer'}, 'Unauthorized\n');
        return;  // skip content handler
    }
    req.ctx.claims = result.claims;  // pass data to content handler
});

loc.handler = function(r) {
    var user = r.ctx.claims.user;  // set by the hook
    r.respond(200, {}, 'Hello, ' + user + '\n');
};
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Health endpoint — no auth required
curl http://127.0.0.1:8180/health/
# → ok

# No token → 401
curl -i http://127.0.0.1:8180/api/
# → HTTP/1.1 401 Unauthorized

# Mint a valid user token
TOKEN=$(curl -s 'http://127.0.0.1:8180/dev/token/?user=alice&role=user&ttl=60' \
        | sed 's/.*"token":"\([^"]*\)".*/\1/')

# Access granted
curl -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8180/api/
# → {"message":"Access granted","user":"alice","role":"user"}

# User token rejected on admin endpoint
curl -i -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8180/api/admin/
# → HTTP/1.1 403 Forbidden

# Mint admin token
ADMIN=$(curl -s 'http://127.0.0.1:8180/dev/token/?user=bob&role=admin&ttl=60' \
        | sed 's/.*"token":"\([^"]*\)".*/\1/')
curl -H "Authorization: Bearer $ADMIN" http://127.0.0.1:8180/api/admin/
# → {"message":"Admin access granted","user":"bob","role":"admin"}

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

Token signing is intentionally omitted in this demo — QuickJS does not include
a cryptographic library. In production, signature verification (HMAC-SHA256 or
RS256) would be implemented as a native C function called via the JS FFI. The
structural/expiry/role logic shown here is the layer that runs in pure
JavaScript on top of a verified signature.
