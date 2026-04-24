#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8180
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.3

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected '$expected', got '$actual')"
        FAIL=$((FAIL+1))
    fi
}

# 1. /health/ is exempt from auth — always 200
OUT=$(curl -sf "http://127.0.0.1:$PORT/health/")
check "GET /health/ returns ok (no auth needed)" "ok" "$OUT"

# 2. /api/ with no token → 401
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/")
check "GET /api/ with no token returns 401" "401" "$CODE"

# 3. Mint a valid token with role=user
TOKEN_JSON=$(curl -sf "http://127.0.0.1:$PORT/dev/token/?user=alice&role=user&ttl=60")
TOKEN=$(echo "$TOKEN_JSON" | sed 's/.*"token":"\([^"]*\)".*/\1/')
check "token minted successfully" "alice" "$TOKEN_JSON"

# 4. /api/ with valid user token → 200
OUT=$(curl -sf -H "Authorization: Bearer $TOKEN" "http://127.0.0.1:$PORT/api/")
check "GET /api/ with valid user token returns 200" "Access granted" "$OUT"
check "response includes user identity" "alice" "$OUT"

# 5. /api/admin/ with user token (role=user) → 403
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Authorization: Bearer $TOKEN" \
    "http://127.0.0.1:$PORT/api/admin/")
check "GET /api/admin/ with user role returns 403" "403" "$CODE"

# 6. Mint an admin token
ADMIN_JSON=$(curl -sf "http://127.0.0.1:$PORT/dev/token/?user=bob&role=admin&ttl=60")
ADMIN_TOKEN=$(echo "$ADMIN_JSON" | sed 's/.*"token":"\([^"]*\)".*/\1/')

# 7. /api/admin/ with admin token → 200
OUT=$(curl -sf -H "Authorization: Bearer $ADMIN_TOKEN" "http://127.0.0.1:$PORT/api/admin/")
check "GET /api/admin/ with admin role returns 200" "Admin access granted" "$OUT"
check "admin response includes user identity" "bob" "$OUT"

# 8. Expired token (ttl=0) → 403
EXPIRED_JSON=$(curl -sf "http://127.0.0.1:$PORT/dev/token/?user=eve&role=user&ttl=0")
EXPIRED_TOKEN=$(echo "$EXPIRED_JSON" | sed 's/.*"token":"\([^"]*\)".*/\1/')
sleep 1
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Authorization: Bearer $EXPIRED_TOKEN" \
    "http://127.0.0.1:$PORT/api/")
check "expired token returns 403" "403" "$CODE"

# 9. Malformed token → 401
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Authorization: Bearer not-a-valid-token!!!" \
    "http://127.0.0.1:$PORT/api/")
check "malformed token returns 401" "401" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
