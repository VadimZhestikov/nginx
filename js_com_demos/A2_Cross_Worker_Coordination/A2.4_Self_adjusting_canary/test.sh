#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8111
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

# 1. Stable backend always works
OUT=$(curl -sf http://127.0.0.1:$PORT/backend/stable/)
check "stable backend returns stable-v1" "stable-v1" "$OUT"

# 2. Canary backend works normally
OUT=$(curl -sf http://127.0.0.1:$PORT/backend/canary/)
check "canary backend returns canary-v2" "canary-v2" "$OUT"

# 3. Initial status shows 10% weight
OUT=$(curl -sf http://127.0.0.1:$PORT/admin/status/)
check "initial canary weight is 10" '"canaryWeight":10' "$OUT"
check "initial canary is live" '"canaryLive":true' "$OUT"

# 4. Manually set canary to 50%
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/weight/?50")
check "set weight to 50%" "canary-weight=50%" "$OUT"

# 5. Status shows new weight
OUT=$(curl -sf http://127.0.0.1:$PORT/admin/status/)
check "status shows canaryWeight=50" '"canaryWeight":50' "$OUT"

# 6. Report 3 errors → auto-kill canary
for i in 1 2 3; do
    curl -sf -X POST http://127.0.0.1:$PORT/admin/report-error/ > /dev/null
done

# 7. Canary should now be at 0% (killed automatically)
OUT=$(curl -sf http://127.0.0.1:$PORT/admin/status/)
check "canary weight auto-set to 0 after threshold" '"canaryWeight":0' "$OUT"
check "canary is not live after threshold" '"canaryLive":false' "$OUT"

# 8. Restore canary weight
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/weight/?20")
check "restore canary to 20%" "canary-weight=20%" "$OUT"

OUT=$(curl -sf http://127.0.0.1:$PORT/admin/status/)
check "status shows canaryWeight=20 after restore" '"canaryWeight":20' "$OUT"

# 9. Disable canary completely
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/weight/?0")
check "set weight to 0" "canary-weight=0%" "$OUT"

# 10. /api/ responds (all traffic goes to stable when weight=0)
OUT=$(curl -sf http://127.0.0.1:$PORT/api/)
check "/api/ responds when canary weight is 0" "stable-v1" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
