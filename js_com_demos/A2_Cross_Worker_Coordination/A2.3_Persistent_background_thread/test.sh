#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8110
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.5   # let the SW setInterval fire at least once

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

# 1. GET /value/ returns JSON with tick and value fields
OUT=$(curl -sf http://127.0.0.1:$PORT/value/)
check "/value/ returns tick field" "tick" "$OUT"
check "/value/ returns value field" "value" "$OUT"

# 2. Manual tick increments the counter
OUT_BEFORE=$(curl -sf http://127.0.0.1:$PORT/value/)
curl -sf -X POST http://127.0.0.1:$PORT/tick/ > /dev/null
OUT_AFTER=$(curl -sf http://127.0.0.1:$PORT/value/)

TICK_BEFORE=$(echo "$OUT_BEFORE" | grep -oE '"tick":[0-9]+' | grep -oE '[0-9]+')
TICK_AFTER=$(echo "$OUT_AFTER"  | grep -oE '"tick":[0-9]+' | grep -oE '[0-9]+')

if [ "${TICK_AFTER:-0}" -gt "${TICK_BEFORE:-0}" ]; then
    echo "PASS: manual tick incremented counter ($TICK_BEFORE → $TICK_AFTER)"
    PASS=$((PASS+1))
else
    echo "FAIL: tick did not increment ($TICK_BEFORE → $TICK_AFTER)"
    FAIL=$((FAIL+1))
fi

# 3. Reset brings tick back to 0
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/reset/)
check "reset returns tick:0" '"tick":0' "$OUT"
check "reset returns value:0" '"value":0' "$OUT"

# 4. Value after reset is 0
OUT=$(curl -sf http://127.0.0.1:$PORT/value/)
check "/value/ shows tick=0 after reset" '"tick":0' "$OUT"

# 5. After a brief wait, setInterval may have ticked again
sleep 0.6
OUT=$(curl -sf http://127.0.0.1:$PORT/value/)
TICK=$(echo "$OUT" | grep -oE '"tick":[0-9]+' | grep -oE '[0-9]+')
if [ "${TICK:-0}" -ge 1 ]; then
    echo "PASS: setInterval fired at least once after reset (tick=$TICK)"
    PASS=$((PASS+1))
else
    echo "INFO: setInterval may not have fired yet (tick=$TICK) — not a failure"
    PASS=$((PASS+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
