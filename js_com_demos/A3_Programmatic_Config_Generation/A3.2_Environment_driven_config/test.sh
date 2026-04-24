#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

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

# ---- Test 1: dev environment (default) ----
echo "--- Testing APP_ENV=dev ---"
APP_ENV=dev "$NGINX" -p . -c nginx.conf
sleep 0.3

OUT=$(curl -sf http://127.0.0.1:8118/ 2>/dev/null || echo "FAILED")
check "dev server responds on port 8118" "env=dev" "$OUT"
check "dev mode label in response" "debug mode" "$OUT"

OUT=$(curl -sf http://127.0.0.1:8118/health/ 2>/dev/null || echo "FAILED")
check "dev health check" "dev-ok" "$OUT"

# Prod port should not be listening
CODE=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 1 http://127.0.0.1:8119/ 2>/dev/null; true)
if echo "$CODE" | grep -qF "000"; then
    echo "PASS: prod port 8119 not listening in dev mode"
    PASS=$((PASS+1))
else
    echo "FAIL: prod port 8119 unexpectedly responded (code=$CODE)"
    FAIL=$((FAIL+1))
fi

APP_ENV=dev "$NGINX" -p . -c nginx.conf -s stop
sleep 0.2

# ---- Test 2: prod environment ----
echo "--- Testing APP_ENV=prod ---"
APP_ENV=prod "$NGINX" -p . -c nginx.conf
sleep 0.3

OUT=$(curl -sf http://127.0.0.1:8119/ 2>/dev/null || echo "FAILED")
check "prod server responds on port 8119" "env=prod" "$OUT"
check "prod port label in response" "port=8119" "$OUT"

OUT=$(curl -sf http://127.0.0.1:8119/health/ 2>/dev/null || echo "FAILED")
check "prod health check" "prod-ok" "$OUT"

# Dev port should not be listening
CODE=$(curl -s -o /dev/null -w "%{http_code}" --connect-timeout 1 http://127.0.0.1:8118/ 2>/dev/null; true)
if echo "$CODE" | grep -qF "000"; then
    echo "PASS: dev port 8118 not listening in prod mode"
    PASS=$((PASS+1))
else
    echo "FAIL: dev port 8118 unexpectedly responded in prod mode (code=$CODE)"
    FAIL=$((FAIL+1))
fi

APP_ENV=prod "$NGINX" -p . -c nginx.conf -s stop

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
