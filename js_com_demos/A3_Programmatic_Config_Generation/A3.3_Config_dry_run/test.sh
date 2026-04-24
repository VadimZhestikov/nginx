#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8120
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

# Real request tests (actual location handlers)
OUT=$(curl -sf http://127.0.0.1:$PORT/health)
check "GET /health returns healthy" "healthy" "$OUT"

OUT=$(curl -sf http://127.0.0.1:$PORT/api/v1/users)
check "GET /api/v1/users routes to API v1" "API v1" "$OUT"

OUT=$(curl -sf http://127.0.0.1:$PORT/api/v2/orders)
check "GET /api/v2/orders routes to API v2" "API v2" "$OUT"

OUT=$(curl -sf http://127.0.0.1:$PORT/static/img/logo.png)
check "GET /static/ routes to static" "static file" "$OUT"

# Dry-run match simulation
OUT=$(curl -sf "http://127.0.0.1:$PORT/match/?/health")
check "/match/ for /health returns exact match" '"matched":"= /health"' "$OUT"

OUT=$(curl -sf "http://127.0.0.1:$PORT/match/?/api/v1/users")
check "/match/ for /api/v1/users returns /api/v1/" '"/api/v1/"' "$OUT"

OUT=$(curl -sf "http://127.0.0.1:$PORT/match/?/api/v2/orders")
check "/match/ for /api/v2/ returns /api/v2/" '"/api/v2/"' "$OUT"

OUT=$(curl -sf "http://127.0.0.1:$PORT/match/?/api/legacy")
check "/match/ for /api/legacy falls back to /api/" '"/api/"' "$OUT"

OUT=$(curl -sf "http://127.0.0.1:$PORT/match/?/static/css/main.css")
check "/match/ for /static/ uses preferential prefix" '"/static/"' "$OUT"

OUT=$(curl -sf "http://127.0.0.1:$PORT/match/?/unknown/path")
check "/match/ for unknown path falls back to root /" '"/"' "$OUT"

# No URI provided
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/match/")
check "no URI returns 400" "400" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
