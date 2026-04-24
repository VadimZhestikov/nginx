#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8105
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

# 1. Zero dynamic servers initially
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "initial dynamic server count is 0" "0" "$OUT"

# 2. Create first tenant
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/create/?tenant-a.local")
check "create tenant-a returns created" "created" "$OUT"

# 3. Count is now 1
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "count is 1 after first create" "1" "$OUT"

# 4. Tenant-a is reachable
OUT=$(curl -sf -H "Host: tenant-a.local" http://127.0.0.1:$PORT/)
check "tenant-a.local serves requests" "tenant: tenant-a.local" "$OUT"

# 5. Create second tenant
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/create/?tenant-b.local")
check "create tenant-b returns created" "created" "$OUT"

# 6. Count is 2
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "count is 2 after second create" "2" "$OUT"

# 7. Remove tenant-a (zero traffic since sync handler decrements immediately)
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/remove/?tenant-a.local")
check "remove tenant-a succeeds" "removed" "$OUT"

# 8. Count drops to 1
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "count is 1 after removal" "1" "$OUT"

# 9. tenant-a no longer reachable
CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: tenant-a.local" http://127.0.0.1:$PORT/)
check "tenant-a.local returns 404 after removal" "404" "$CODE"

# 10. tenant-b still alive
OUT=$(curl -sf -H "Host: tenant-b.local" http://127.0.0.1:$PORT/)
check "tenant-b.local still serves after tenant-a removed" "tenant: tenant-b.local" "$OUT"

# 11. Remove nonexistent returns 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:$PORT/remove/?ghost.local")
check "remove nonexistent returns 404" "404" "$CODE"

# 12. Duplicate create is idempotent
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/create/?tenant-b.local")
check "duplicate create returns already-exists" "already-exists" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
