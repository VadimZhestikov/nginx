# B2.1 — Inline JWT Validation

## What it shows

Decodes and validates a JWT-structured token **inside nginx**, without calling
an external auth service.  The JavaScript performs:

1. Structural check — exactly three base64url segments separated by `.`
2. Base64url-decode of the claims segment (QuickJS has no `atob`, so a helper
   is included)
3. JSON-parse the claims object
4. Presence check for `sub` (subject)
5. Expiry check — `exp` must be in the future

A `/admin/token/` endpoint issues demo tokens for testing.

**Note:** Signature verification is omitted here because QuickJS does not ship
with a native crypto library.  In a real deployment a C-level HMAC-SHA256 call
would be added; the structural + expiry logic shown here is pure JavaScript.

## Classic nginx comparison

Classic nginx has no ability to parse a JWT.  The standard approach is to use
`auth_request` to proxy every request to a separate auth microservice.  That
adds a full network round-trip per request plus operational complexity of
running another process.  With the JS module the validation runs in the same
event-loop tick as the request, at zero extra latency.

## How to run

```bash
cd B2.1_Inline_JWT_validation
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Get a token for "alice" valid for 60 seconds
TOKEN=$(curl -s "http://localhost:8135/admin/token/?sub=alice&ttl=60")
echo "Token: $TOKEN"

# Access protected resource with valid token
curl -H "Authorization: Bearer $TOKEN" http://localhost:8135/protected/

# Try with a bad token
curl -H "Authorization: Bearer garbage" http://localhost:8135/protected/

# Get an already-expired token
EXPIRED=$(curl -s "http://localhost:8135/admin/token/?sub=bob&ttl=0")
sleep 1
curl -H "Authorization: Bearer $EXPIRED" http://localhost:8135/protected/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
