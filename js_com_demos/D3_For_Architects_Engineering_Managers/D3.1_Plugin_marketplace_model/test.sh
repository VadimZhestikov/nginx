#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8183
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

# 1. GET /plugins/ — list contains installed plugins
OUT=$(curl -sf "http://127.0.0.1:$PORT/plugins/")
check "plugins list contains ratelimit" "ratelimit" "$OUT"
check "plugins list contains cors" "cors" "$OUT"
check "plugins list contains telemetry" "telemetry" "$OUT"

# 2. GET /api/ — CORS headers added by cors@2.1 plugin
OUT=$(curl -si "http://127.0.0.1:$PORT/api/" | tr -d '\r')
check "cors plugin adds Access-Control-Allow-Origin" "Access-Control-Allow-Origin" "$OUT"
check "Access-Control-Allow-Origin is wildcard" "*" "$OUT"

# 3. GET /api/ — returns 200
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/")
check_code "GET /api/ returns 200" "200" "$CODE"

# 4. Rate limit header is present
OUT=$(curl -si "http://127.0.0.1:$PORT/api/" | tr -d '\r')
check "ratelimit plugin adds X-RateLimit-Remaining" "X-RateLimit-Remaining" "$OUT"

# 5. Metrics endpoint shows request count
OUT=$(curl -sf "http://127.0.0.1:$PORT/metrics/")
check "telemetry plugin provides api_requests_total" "api_requests_total" "$OUT"
check "metrics shows ratelimit_tokens_remaining" "ratelimit_tokens_remaining" "$OUT"

# 6. OPTIONS preflight returns 204 (cors plugin)
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X OPTIONS "http://127.0.0.1:$PORT/api/")
check_code "OPTIONS preflight returns 204" "204" "$CODE"

# 7. Exhaust rate limit — send 9 more requests (1 already sent in test 3)
for i in $(seq 1 9); do
    curl -s "http://127.0.0.1:$PORT/api/" > /dev/null
done
# Now at limit — next request should be 429
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/")
check_code "rate limit enforced — 429 after 10 requests" "429" "$CODE"

# 8. Metrics reflects rejected requests too
OUT=$(curl -sf "http://127.0.0.1:$PORT/metrics/")
check "metrics shows ratelimit_tokens_remaining 0" '"ratelimit_tokens_remaining": 0' "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
