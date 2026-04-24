#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8130
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

# Test 1: acme tenant + /api/ URI prefix + JSON Accept → acme JSON backend
# X-Original-Uri simulates the upstream path the front-end proxy would forward
OUT=$(curl -s -H "X-Tenant: acme" -H "Accept: application/json" \
    -H "X-Original-Uri: /api/orders" \
    "http://localhost:$PORT/route/")
check "acme + /api/ + json Accept → acme-json" "acme-json" "$OUT"

# Test 2: acme tenant but no JSON accept → acme default (HTML)
OUT=$(curl -s -H "X-Tenant: acme" -H "Accept: text/html" \
    "http://localhost:$PORT/route/")
check "acme + html Accept → acme default" "acme default backend" "$OUT"

# Test 3: no special tenant → default backend
OUT=$(curl -s "http://localhost:$PORT/route/")
check "no tenant → default backend" "default backend" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
