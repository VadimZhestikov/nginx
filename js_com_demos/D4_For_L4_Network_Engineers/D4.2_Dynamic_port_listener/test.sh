#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8191
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

# 1. Initial status — no services registered yet
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "initial status shows 0 registered services" '"registered_services": 0' "$OUT"

# 2. Register first service
OUT=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"name":"svc-alpha","service":"game-server"}' \
    "http://127.0.0.1:$PORT/admin/listen/")
check "first service registered" "registered" "$OUT"
check "first service name echoed" "svc-alpha" "$OUT"

# 3. Service is accessible via Host header
OUT=$(curl -sf -H "Host: svc-alpha" "http://127.0.0.1:$PORT/data/")
check "svc-alpha /data/ responds" "svc-alpha" "$OUT"
check "svc-alpha type is game-server" "game-server" "$OUT"

# 4. Health check works
OUT=$(curl -sf -H "Host: svc-alpha" "http://127.0.0.1:$PORT/health/")
check "svc-alpha /health/ returns ok" "ok" "$OUT"
check "health identifies service" "svc-alpha" "$OUT"

# 5. Register second service
OUT=$(curl -sf -X POST -H "Content-Type: application/json" \
    -d '{"name":"svc-beta","service":"video-relay"}' \
    "http://127.0.0.1:$PORT/admin/listen/")
check "second service registered" "registered" "$OUT"

OUT=$(curl -sf -H "Host: svc-beta" "http://127.0.0.1:$PORT/data/")
check "svc-beta /data/ responds" "video-relay" "$OUT"

# 6. Status shows both services
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows 2 registered services" '"registered_services": 2' "$OUT"
check "status includes svc-alpha" "svc-alpha" "$OUT"
check "status includes svc-beta" "svc-beta" "$OUT"

# 7. Duplicate registration returns 409
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    -H "Content-Type: application/json" \
    -d '{"name":"svc-alpha","service":"duplicate"}' \
    "http://127.0.0.1:$PORT/admin/listen/")
check_code "duplicate service returns 409" "409" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
