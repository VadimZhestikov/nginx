#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8148
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

# Test 1: no X-Request-Id supplied → one is auto-generated in response
OUT=$(curl -s -D - "http://localhost:$PORT/api/")
check "auto-generated X-Request-Id in response" "x-request-id:" "$OUT"

# Test 2: supplied X-Request-Id is echoed back
OUT=$(curl -s -D - -H "X-Request-Id: abc-123-xyz" "http://localhost:$PORT/api/")
check "incoming X-Request-Id echoed in response" "abc-123-xyz" "$OUT"

# Test 3: body also contains the request-id
OUT=$(curl -s -H "X-Request-Id: trace-me-456" "http://localhost:$PORT/api/")
check "body contains request-id" "trace-me-456" "$OUT"

# Test 4: two requests without ids get different ids
ID1=$(curl -sD - "http://localhost:$PORT/api/" | grep -i "x-request-id:" | tr -d '\r\n')
ID2=$(curl -sD - "http://localhost:$PORT/api/" | grep -i "x-request-id:" | tr -d '\r\n')
if [ "$ID1" != "$ID2" ]; then
    echo "PASS: auto-generated ids are unique"
    PASS=$((PASS+1))
else
    echo "FAIL: auto-generated ids are the same ($ID1)"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
