#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8172
PASS=0; FAIL=0
cd "$DEMO_DIR"
mkdir -p logs

# Check if qjs is available
command -v qjs || { echo 'qjs not found — install quickjs (make -C ../../../quickjs install)'; exit 0; }

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

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

# ── Part 1: unit tests with qjs (no nginx needed) ─────────────────────────
echo "=== Running unit tests with qjs ==="
if qjs --std route-test.js; then
    echo "PASS: qjs unit tests all passed"
    PASS=$((PASS+1))
else
    echo "FAIL: qjs unit tests had failures"
    FAIL=$((FAIL+1))
fi

# ── Part 2: same logic running inside nginx ────────────────────────────────
echo ""
echo "=== Running same logic inside nginx ==="
"$NGINX" -p . -c nginx.conf
sleep 0.3

R1=$(curl -s -H "X-Target-Uri: /api/users" -H "Accept: application/json" \
    "http://localhost:${PORT}/route/")
check "nginx: /api/ + JSON → json_backend" '"backend":"json_backend"' "$R1"

R2=$(curl -s -H "X-Target-Uri: /static/logo.png" "http://localhost:${PORT}/route/")
check "nginx: /static/ → cdn_backend" '"backend":"cdn_backend"' "$R2"

R3=$(curl -s "http://localhost:${PORT}/route/?uri=%2Fadmin%2Fsettings")
check "nginx: /admin/ no x-internal → forbidden" '"backend":"forbidden"' "$R3"

R4=$(curl -s -H "x-internal: true" \
    "http://localhost:${PORT}/route/?uri=%2Fadmin%2Fsettings")
check "nginx: /admin/ + x-internal → admin_backend" '"backend":"admin_backend"' "$R4"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
