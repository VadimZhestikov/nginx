#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8134
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

# Test 1: beta cookie → always gets landing B regardless of time
OUT=$(curl -s -b "segment=beta" "http://localhost:$PORT/landing/")
check "segment=beta → landing B" "landing B" "$OUT"

# Test 2: no cookie → landing A variant (day or evening, either is valid)
OUT=$(curl -s "http://localhost:$PORT/landing/")
check "no cookie → landing A variant" "landing A" "$OUT"

# Test 3: X-Variant response header is set
OUT=$(curl -s -I -b "segment=beta" "http://localhost:$PORT/landing/")
check "X-Variant header present" "X-Variant: landing B" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
