#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

stop_nginx() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; sleep 0.2; }
cleanup() { stop_nginx; }
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

check_absent() {
    local desc="$1" absent="$2" actual="$3"
    if echo "$actual" | grep -qF "$absent"; then
        echo "FAIL: $desc (expected '$absent' to be ABSENT, but it was present)"
        FAIL=$((FAIL+1))
    else
        echo "PASS: $desc"
        PASS=$((PASS+1))
    fi
}

# ============================================================
# Round 1: dev environment
# ============================================================
echo "--- Testing APP_ENV=dev (port 8184) ---"
DEV_PORT=8184

APP_ENV=dev "$NGINX" -p . -c nginx.conf
sleep 0.3

# 1. /env/ returns "dev"
OUT=$(curl -sf "http://127.0.0.1:$DEV_PORT/env/")
check "dev: /env/ returns dev" "dev" "$OUT"

# 2. /api/ returns verbose debug info
OUT=$(curl -sf "http://127.0.0.1:$DEV_PORT/api/")
check "dev: /api/ has environment=dev" "dev" "$OUT"
check "dev: /api/ has debug_info" "debug_info" "$OUT"
check "dev: /api/ has X-Debug header" "X-Debug" "$(curl -si "http://127.0.0.1:$DEV_PORT/api/" | tr -d '\r')"

# 3. /debug/ endpoint exists in dev
OUT=$(curl -sf "http://127.0.0.1:$DEV_PORT/debug/")
check "dev: /debug/ endpoint exists" "nginx_version" "$OUT"

# 4. /error/ returns verbose error
OUT=$(curl -s "http://127.0.0.1:$DEV_PORT/error/")
check "dev: /error/ returns detailed message" "detail" "$OUT"

stop_nginx

# ============================================================
# Round 2: prod environment
# ============================================================
echo "--- Testing APP_ENV=prod (port 8185) ---"
PROD_PORT=8185

APP_ENV=prod "$NGINX" -p . -c nginx.conf
sleep 0.3

# 5. /env/ returns "prod"
OUT=$(curl -sf "http://127.0.0.1:$PROD_PORT/env/")
check "prod: /env/ returns prod" "prod" "$OUT"

# 6. /api/ returns minimal response (no debug_info)
OUT=$(curl -sf "http://127.0.0.1:$PROD_PORT/api/")
check "prod: /api/ returns OK" "OK" "$OUT"
check_absent "prod: /api/ has no debug_info" "debug_info" "$OUT"
check_absent "prod: /api/ has no verbose details" "worker_idx" "$OUT"

# 7. /debug/ does NOT exist in prod — returns 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PROD_PORT/debug/")
check "prod: /debug/ returns 404 (not provisioned)" "404" "$CODE"

# 8. /error/ returns generic message in prod
OUT=$(curl -s "http://127.0.0.1:$PROD_PORT/error/")
check "prod: /error/ returns generic message" "Internal Server Error" "$OUT"
check_absent "prod: /error/ does not expose internals" "detail" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
