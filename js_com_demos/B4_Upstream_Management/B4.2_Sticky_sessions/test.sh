#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8144
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

# Test 1: same token always routes to same backend (determinism)
R1=$(curl -s -H "X-Session-Token: user-abc-token" "http://localhost:$PORT/sticky/")
R2=$(curl -s -H "X-Session-Token: user-abc-token" "http://localhost:$PORT/sticky/")
R3=$(curl -s -H "X-Session-Token: user-abc-token" "http://localhost:$PORT/sticky/")
check "same token → same backend (req 1 vs 2)" "$R1" "$R2"
check "same token → same backend (req 2 vs 3)" "$R2" "$R3"

# Test 2: X-Backend header is set in response
OUT=$(curl -s -D - -H "X-Session-Token: user-abc-token" "http://localhost:$PORT/sticky/")
check "X-Backend header present" "X-Backend:" "$OUT"

# Test 3: response body is from a valid backend
check "backend_a or backend_b in response" "backend_" "$R1"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
