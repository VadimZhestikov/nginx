#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8135
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

# Test 1: obtain a token
TOKEN=$(curl -s "http://localhost:$PORT/admin/token/?sub=alice&ttl=60")
check "token issued" "." "$TOKEN"

# Test 2: use valid token → 200 + claims
OUT=$(curl -s -H "Authorization: Bearer $TOKEN" "http://localhost:$PORT/protected/")
check "valid token → access granted" "access granted" "$OUT"
check "valid token → sub=alice" "alice" "$OUT"

# Test 3: bad token → 401
OUT=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer bad.token.here" \
    "http://localhost:$PORT/protected/")
check "bad token → 401" "401" "$OUT"

# Test 4: expired token (ttl=0 makes it expire immediately)
EXPIRED=$(curl -s "http://localhost:$PORT/admin/token/?sub=bob&ttl=0")
sleep 1
OUT=$(curl -s -w "\n%{http_code}" -H "Authorization: Bearer $EXPIRED" \
    "http://localhost:$PORT/protected/")
check "expired token → 401" "401" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
