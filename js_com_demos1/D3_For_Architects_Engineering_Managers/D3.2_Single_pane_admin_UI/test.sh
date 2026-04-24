#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8186
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

# 1. JSON status endpoint works
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status returns nginx_version" "nginx_version" "$OUT"
check "status returns server_count" "server_count" "$OUT"

# 2. Info endpoint explains where the full UI is
OUT=$(curl -sf "http://127.0.0.1:$PORT/info/")
check "info points to admin_ui_demo_v2" "admin_ui_demo_v2" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
echo ""
echo "NOTE: This is a placeholder. See README.md for the full admin UI."
[ "$FAIL" -eq 0 ]
