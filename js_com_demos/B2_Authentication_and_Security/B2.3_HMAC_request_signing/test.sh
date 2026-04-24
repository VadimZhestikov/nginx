#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8137
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

# Test 1: response includes X-Signature header
OUT=$(curl -s -D - "http://localhost:$PORT/sign/?path=/api/orders")
check "X-Signature header present" "X-Signature:" "$OUT"

# Test 2: response body contains Hmac-SHA256 authorization string
OUT=$(curl -s "http://localhost:$PORT/sign/?path=/api/orders")
check "authorization field present" "Hmac-SHA256" "$OUT"
check "credential present" "AKIAIOSFODNN7EXAMPLE" "$OUT"

# Test 3: same path produces same signature (deterministic hash)
SIG1=$(curl -sD - "http://localhost:$PORT/sign/?path=/api/orders" | grep -i "^X-Signature:" | tr -d '\r')
SIG2=$(curl -sD - "http://localhost:$PORT/sign/?path=/api/orders" | grep -i "^X-Signature:" | tr -d '\r')
if [ -n "$SIG1" ] && [ "$SIG1" = "$SIG2" ]; then
    echo "PASS: same path → same signature"
    PASS=$((PASS+1))
else
    echo "FAIL: same path → same signature (got '$SIG1' vs '$SIG2')"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
