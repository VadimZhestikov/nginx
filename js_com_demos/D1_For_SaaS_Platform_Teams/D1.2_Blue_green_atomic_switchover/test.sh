#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8176
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

# 1. Initial state: blue is active
OUT=$(curl -sf "http://127.0.0.1:$PORT/app/")
check "initial /app/ routes to blue" "blue" "$OUT"

# 2. Status confirms blue is active
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows blue active" "blue" "$OUT"
check "status shows v1.0" "v1.0" "$OUT"

# 3. Switch to green
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/switch/")
check "switch responds with switched=true" "switched" "$OUT"
check "switch reports green active" "green" "$OUT"

# 4. After switch: /app/ routes to green
OUT=$(curl -sf "http://127.0.0.1:$PORT/app/")
check "after switch /app/ routes to green" "green" "$OUT"
check "green response has v2.0" "v2.0" "$OUT"

# 5. Status confirms green is active
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows green after switch" "green" "$OUT"

# 6. Switch back to blue
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/switch/")
check "second switch reports blue" "blue" "$OUT"

# 7. /app/ routes back to blue
OUT=$(curl -sf "http://127.0.0.1:$PORT/app/")
check "after second switch /app/ routes to blue" "blue" "$OUT"
check "blue response has v1.0" "v1.0" "$OUT"

# 8. Multiple rapid switches are safe (atomic compareExchange)
curl -s -X POST "http://127.0.0.1:$PORT/admin/switch/" > /dev/null
curl -s -X POST "http://127.0.0.1:$PORT/admin/switch/" > /dev/null
OUT=$(curl -sf "http://127.0.0.1:$PORT/app/")
check "after two rapid switches back to blue" "blue" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
