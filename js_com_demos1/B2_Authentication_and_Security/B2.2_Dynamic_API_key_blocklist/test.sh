#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8136
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

# Test 1: valid key → 200
OUT=$(curl -s -H "X-API-Key: my-valid-key-123" "http://localhost:$PORT/api/")
check "valid key → OK" "OK: key accepted" "$OUT"

# Test 2: key pre-revoked at startup → 403
OUT=$(curl -s -H "X-API-Key: revoked-at-startup" "http://localhost:$PORT/api/")
check "pre-revoked key → Forbidden" "Forbidden: key revoked" "$OUT"

# Test 3: revoke a key at runtime, then check it
curl -s -X POST "http://localhost:$PORT/admin/revoke/?key=temp-key-abc" > /dev/null
OUT=$(curl -s -H "X-API-Key: temp-key-abc" "http://localhost:$PORT/api/")
check "runtime-revoked key → Forbidden" "Forbidden: key revoked" "$OUT"

# Test 4: different valid key still works after revocation
OUT=$(curl -s -H "X-API-Key: other-valid-key" "http://localhost:$PORT/api/")
check "other key still valid" "OK: key accepted" "$OUT"

# Test 5: missing key → 401
OUT=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT/api/")
check "missing key → 401" "401" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
