#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8179
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

# 1. Refill the bucket
OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/refill/")
check "refill returns quota info" "refilled" "$OUT"

# 2. Status shows full bucket
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows 10 tokens remaining" "10" "$OUT"

# 3. Send exactly 10 requests — all should be 200
echo "Sending 10 requests (all should be 200)..."
ALL_OK=1
for i in $(seq 1 10); do
    CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/")
    if [ "$CODE" != "200" ]; then
        echo "FAIL: request $i returned $CODE (expected 200)"
        ALL_OK=0
        FAIL=$((FAIL+1))
    fi
done
if [ "$ALL_OK" -eq 1 ]; then
    echo "PASS: all 10 quota requests returned 200"
    PASS=$((PASS+1))
fi

# 4. 11th request must be 429
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/")
check_code "11th request returns 429 (quota exhausted)" "429" "$CODE"

# 5. Status shows 0 tokens remaining
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows 0 tokens after exhaustion" '"tokens_remaining": 0' "$OUT"

# 6. Refill restores the bucket
OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/refill/")
check "second refill succeeds" "refilled" "$OUT"

# 7. First request after refill is 200 again
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/api/")
check_code "first request after refill is 200" "200" "$CODE"

# 8. Check X-Quota-Remaining header decrements
OUT=$(curl -si "http://127.0.0.1:$PORT/api/" | tr -d '\r')
check "X-Quota-Remaining header present" "X-Quota-Remaining" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
