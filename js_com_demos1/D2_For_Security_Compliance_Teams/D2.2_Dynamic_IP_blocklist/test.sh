#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8181
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

check_code() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected HTTP $expected, got $actual)"
        FAIL=$((FAIL+1))
    fi
}

# 1. GET /api/ with clean IP — should pass
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Real-IP: 10.0.0.1" \
    "http://127.0.0.1:$PORT/api/")
check_code "clean IP 10.0.0.1 gets 200" "200" "$CODE"

# 2. GET /api/ with another clean IP — should pass
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Real-IP: 10.0.0.99" \
    "http://127.0.0.1:$PORT/api/")
check_code "10.0.0.99 not yet blocked — 200" "200" "$CODE"

# 3. Block 10.0.0.99
OUT=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"ip":"10.0.0.99"}' \
    "http://127.0.0.1:$PORT/admin/block/")
check "block returns blocked IP" "10.0.0.99" "$OUT"

# 4. GET /api/ with blocked IP — should be 403
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Real-IP: 10.0.0.99" \
    "http://127.0.0.1:$PORT/api/")
check_code "blocked IP 10.0.0.99 gets 403" "403" "$CODE"

# 5. Clean IP still passes after block
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Real-IP: 10.0.0.1" \
    "http://127.0.0.1:$PORT/api/")
check_code "10.0.0.1 still gets 200 after block" "200" "$CODE"

# 6. Blocklist shows the blocked IP
OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/blocklist/")
check "blocklist includes 10.0.0.99" "10.0.0.99" "$OUT"
check "blocklist count is 1" '"count": 1' "$OUT"

# 7. Block a second IP
curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"ip":"192.168.1.100"}' \
    "http://127.0.0.1:$PORT/admin/block/" > /dev/null
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Real-IP: 192.168.1.100" \
    "http://127.0.0.1:$PORT/api/")
check_code "second blocked IP gets 403" "403" "$CODE"

# 8. Unblock 10.0.0.99
OUT=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"ip":"10.0.0.99"}' \
    "http://127.0.0.1:$PORT/admin/unblock/")
check "unblock returns was_blocked=true" "true" "$OUT"

# 9. Unblocked IP is now allowed again
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Real-IP: 10.0.0.99" \
    "http://127.0.0.1:$PORT/api/")
check_code "unblocked IP 10.0.0.99 gets 200 again" "200" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
