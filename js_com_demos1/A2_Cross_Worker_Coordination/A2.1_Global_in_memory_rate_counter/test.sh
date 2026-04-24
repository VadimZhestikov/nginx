#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8108
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

# 1. Initial count is 0
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "initial counter is 0" "total=0" "$OUT"

# 2. Send 5 hit requests
for i in 1 2 3 4 5; do
    curl -sf http://127.0.0.1:$PORT/hit/ > /dev/null
done

# 3. Count should be 5
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "counter is 5 after 5 hits" "total=5" "$OUT"

# 4. Each hit response contains a hit number
OUT=$(curl -sf http://127.0.0.1:$PORT/hit/)
check "hit response contains hit number" "hit #" "$OUT"

# 5. Send 5 more hits concurrently (tests atomicity with 2 workers)
for i in 1 2 3 4 5; do
    curl -sf http://127.0.0.1:$PORT/hit/ > /dev/null &
done
wait

# 6. Count should be >= 11 (6 + 5 sequential, plus the background batch)
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
TOTAL=$(echo "$OUT" | grep -oE 'total=[0-9]+' | grep -oE '[0-9]+')
if [ "${TOTAL:-0}" -ge 11 ]; then
    echo "PASS: counter >= 11 after concurrent hits (got $TOTAL)"
    PASS=$((PASS+1))
else
    echo "FAIL: expected counter >= 11, got $TOTAL"
    FAIL=$((FAIL+1))
fi

# 7. Reset counter
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/reset/)
check "reset returns reset" "reset" "$OUT"

# 8. Counter is 0 after reset
OUT=$(curl -sf http://127.0.0.1:$PORT/count/)
check "counter is 0 after reset" "total=0" "$OUT"

# 9. Hit after reset starts from 1
OUT=$(curl -sf http://127.0.0.1:$PORT/hit/)
check "first hit after reset is #1" "hit #1" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
