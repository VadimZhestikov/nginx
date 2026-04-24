#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8162
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

# Route lookups
R1=$(curl -s -H "X-Target-Uri: /api/v2/users" "http://localhost:${PORT}/route/")
echo "Route /api/v2/: $R1"
check "/api/v2/ routes to backend_v2"    '"backend":"backend_v2"'    "$R1"

R2=$(curl -s -H "X-Target-Uri: /api/v1/items" "http://localhost:${PORT}/route/")
echo "Route /api/v1/: $R2"
check "/api/v1/ routes to backend_v1"    '"backend":"backend_v1"'    "$R2"

R3=$(curl -s -H "X-Target-Uri: /admin/settings" "http://localhost:${PORT}/route/")
echo "Route /admin/: $R3"
check "/admin/ routes to backend_admin"  '"backend":"backend_admin"' "$R3"

R4=$(curl -s -H "X-Target-Uri: /static/logo.png" "http://localhost:${PORT}/route/")
echo "Route /static/: $R4"
check "/static/ routes to backend_cdn"  '"backend":"backend_cdn"'   "$R4"

R5=$(curl -s -H "X-Target-Uri: /health/" "http://localhost:${PORT}/route/")
echo "Route /health/: $R5"
check "/health/ routes to backend_health" '"backend":"backend_health"' "$R5"

R6=$(curl -s -H "X-Target-Uri: /unknown/path" "http://localhost:${PORT}/route/")
echo "Route /unknown/: $R6"
check "unknown prefix falls back to default_backend" '"backend":"default_backend"' "$R6"

# Admin route list
ROUTES=$(curl -s "http://localhost:${PORT}/admin/routes/")
echo "All routes: $ROUTES"
check "route list contains /api/v2/" '"/api/v2/"' "$ROUTES"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
