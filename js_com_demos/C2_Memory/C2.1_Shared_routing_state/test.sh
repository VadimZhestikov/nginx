#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8163
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

# Before any routes are set, should return "default"
R1=$(curl -s -H "X-Path: /api/" "http://localhost:${PORT}/route/")
echo "Before set: $R1"
check "unset route returns default" '"backend":"default"' "$R1"

# Set a route
SET=$(curl -s "http://localhost:${PORT}/admin/set-route/?path=%2Fapi%2F&backend=v2")
echo "Set route: $SET"
check "set-route succeeds" "route set" "$SET"

# Read it back (may hit a different worker due to 2 workers)
R2=$(curl -s -H "X-Path: /api/" "http://localhost:${PORT}/route/")
echo "After set: $R2"
check "route is now v2" '"backend":"v2"' "$R2"

# Set another route
curl -s "http://localhost:${PORT}/admin/set-route/?path=%2Fstatic%2F&backend=cdn" > /dev/null

# List routes
LIST=$(curl -s "http://localhost:${PORT}/admin/list-routes/")
echo "Route list: $LIST"
check "list contains /api/" '"/api/"' "$LIST"
check "list contains /static/" '"/static/"' "$LIST"
check "list shows v2 backend" '"v2"' "$LIST"
check "list shows cdn backend" '"cdn"' "$LIST"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
