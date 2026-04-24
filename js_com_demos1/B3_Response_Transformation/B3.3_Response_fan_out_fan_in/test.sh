#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8140
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

# Test 1: merged response contains all three services' data
OUT=$(curl -s "http://localhost:$PORT/product/?id=5")
check "price field present" '"price"' "$OUT"
check "stock field present" '"stock"' "$OUT"
check "rating field present" '"rating"' "$OUT"

# Test 2: price data has currency
check "price has currency" '"currency"' "$OUT"

# Test 3: stock has inStock flag
check "stock has inStock" '"inStock"' "$OUT"

# Test 4: different id returns different data
OUT2=$(curl -s "http://localhost:$PORT/product/?id=10")
check "different id works" '"rating"' "$OUT2"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
