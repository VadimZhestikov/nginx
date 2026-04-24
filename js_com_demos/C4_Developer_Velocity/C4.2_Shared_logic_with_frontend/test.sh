#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8173
PASS=0; FAIL=0
cd "$DEMO_DIR"
mkdir -p logs

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

# ── Part 1: browser-context tests (qjs, no nginx) ─────────────────────────
echo "=== Browser-context tests (qjs) ==="
if qjs --std browser-test.js; then
    echo "PASS: browser-test.js all passed"
    PASS=$((PASS+1))
else
    echo "FAIL: browser-test.js had failures"
    FAIL=$((FAIL+1))
fi

# ── Part 2: same function inside nginx ────────────────────────────────────
echo ""
echo "=== nginx integration tests ==="
"$NGINX" -p . -c nginx.conf
sleep 0.3

R1=$(curl -s "http://localhost:${PORT}/normalize/?url=%2FAPI%2F%2FUsers%2F%2FProfile")
echo "Normalize /API//Users//Profile: $R1"
check "normalized to /api/users/profile" '"normalized":"/api/users/profile"' "$R1"

R2=$(curl -s "http://localhost:${PORT}/normalize/?url=%2Fapi%2Fusers%2F")
echo "Normalize /api/users/: $R2"
check "trailing slash removed" '"normalized":"/api/users"' "$R2"

R3=$(curl -s -H "X-Raw-Url: /Page#Section" "http://localhost:${PORT}/normalize/")
echo "Normalize /Page#Section (via header): $R3"
check "hash replaced with _" '"normalized":"/page_section"' "$R3"

R4=$(curl -s "http://localhost:${PORT}/normalize/?url=%2Fstatic%2Fapp.js")
echo "Normalize /static/app.js: $R4"
check "dots preserved" '"normalized":"/static/app.js"' "$R4"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
