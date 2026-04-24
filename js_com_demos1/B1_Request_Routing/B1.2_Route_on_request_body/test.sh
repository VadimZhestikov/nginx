#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8133
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

# Test 1: GetUser operation → users handler
OUT=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"operation":"GetUser"}' \
    "http://localhost:$PORT/graphql/")
check "GetUser → users handler" "users handler" "$OUT"

# Test 2: CreateOrder operation → orders handler
OUT=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"operation":"CreateOrder"}' \
    "http://localhost:$PORT/graphql/")
check "CreateOrder → orders handler" "orders handler" "$OUT"

# Test 3: Unknown operation → 400
OUT=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: application/json" \
    -d '{"operation":"DeleteEverything"}' \
    "http://localhost:$PORT/graphql/")
check "unknown operation → 400" "400" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
