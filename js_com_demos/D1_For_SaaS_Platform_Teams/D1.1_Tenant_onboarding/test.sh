#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8175
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

# 1. GET /status/ — shows initial server count (admin + anchor = 2 servers)
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "initial status returns server_count" "server_count" "$OUT"
check "initial status includes admin.local" "admin.local" "$OUT"

# 2. POST /admin/tenants/ with JSON body — creates new tenant virtual host
OUT=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"name":"acme-corp"}' \
    "http://127.0.0.1:$PORT/admin/tenants/")
check "POST tenant returns status created" "created" "$OUT"
check "POST tenant echoes tenant name" "acme-corp" "$OUT"

# 3. GET /status/ — server count increased after tenant creation
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows new server after provisioning" "acme-corp" "$OUT"

# 4. Access the new tenant's API via Host header
OUT=$(curl -sf -H "Host: acme-corp" "http://127.0.0.1:$PORT/api/")
check "tenant API responds with tenant name" "acme-corp" "$OUT"
check "tenant API has welcome message" "Welcome" "$OUT"

# 5. Tenant health check also works
OUT=$(curl -sf -H "Host: acme-corp" "http://127.0.0.1:$PORT/health/")
check "tenant health check returns ok" "ok" "$OUT"
check "tenant health check identifies tenant" "acme-corp" "$OUT"

# 6. Duplicate tenant creation returns 409 Conflict
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: application/json" \
    -d '{"name":"acme-corp"}' \
    "http://127.0.0.1:$PORT/admin/tenants/")
check "duplicate tenant returns 409" "409" "$CODE"

# 7. Second tenant can be created independently
OUT=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"name":"beta-inc"}' \
    "http://127.0.0.1:$PORT/admin/tenants/")
check "second tenant created" "created" "$OUT"
OUT=$(curl -sf -H "Host: beta-inc" "http://127.0.0.1:$PORT/api/")
check "second tenant API responds" "beta-inc" "$OUT"

# 8. Status shows both tenants
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows both tenants" "acme-corp" "$OUT"
check "status shows beta-inc too" "beta-inc" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
