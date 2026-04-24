#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8150
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

# Make several API requests to populate the histogram
for i in $(seq 1 5); do
    curl -s "http://localhost:$PORT/api/" > /dev/null
done

# Test 1: /metrics/ returns Prometheus text format
OUT=$(curl -s "http://localhost:$PORT/metrics/")
check "histogram metric present" "http_request_duration_bucket" "$OUT"

# Test 2: counter incremented
check "request count > 0" "http_requests_total" "$OUT"

# Test 3: +Inf bucket present
check "+Inf bucket present" 'le="+Inf"' "$OUT"

# Test 4: at least 5 requests counted
COUNT=$(echo "$OUT" | grep '^http_requests_total' | awk '{print $2}')
if [ -n "$COUNT" ] && [ "$COUNT" -ge 5 ] 2>/dev/null; then
    echo "PASS: request count >= 5 (got $COUNT)"
    PASS=$((PASS+1))
else
    echo "FAIL: expected count >= 5, got '$COUNT'"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
