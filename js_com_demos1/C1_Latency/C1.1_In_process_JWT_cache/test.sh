#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8160
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

# Get a token
TOKEN_RESP=$(curl -s "http://localhost:${PORT}/admin/token/?sub=alice")
echo "Token response: $TOKEN_RESP"
TOKEN=$(echo "$TOKEN_RESP" | grep -o '"token":"[^"]*"' | cut -d'"' -f4)
echo "Token: ${TOKEN:0:40}..."

# First request — cache miss
RESP1=$(curl -s -i -H "Authorization: Bearer $TOKEN" \
    "http://localhost:${PORT}/protected/")
check "first request (cache miss) returns 200" "200" "$RESP1"
check "first request has X-Cache: MISS" "MISS" "$RESP1"
check "first request grants access" "access granted" "$RESP1"

# Second request — cache hit (same worker or another worker both see SAB)
RESP2=$(curl -s -i -H "Authorization: Bearer $TOKEN" \
    "http://localhost:${PORT}/protected/")
check "second request returns 200" "200" "$RESP2"
check "second request has X-Cache: HIT" "HIT" "$RESP2"

# Request without token
RESP3=$(curl -s -o /dev/null -w "%{http_code}" \
    "http://localhost:${PORT}/protected/")
check "request without token returns 401" "401" "$RESP3"

# Request with bad token
RESP4=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "Authorization: Bearer bad.token.value" \
    "http://localhost:${PORT}/protected/")
check "request with bad token returns 403" "403" "$RESP4"

# Cache stats — at least 1 slot occupied
STATS=$(curl -s "http://localhost:${PORT}/admin/cache-stats/")
echo "Cache stats: $STATS"
check "cache stats shows cached:1" '"cached":1' "$STATS"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
