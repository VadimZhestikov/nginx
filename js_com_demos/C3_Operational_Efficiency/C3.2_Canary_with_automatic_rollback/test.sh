#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8167
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

# Initially in 'stable' mode
STATUS=$(curl -s "http://localhost:${PORT}/admin/canary-status/")
echo "Initial status: $STATUS"
check "initial mode is stable" '"mode": "stable"' "$STATUS"

# In stable mode, all requests go to stable backend
R1=$(curl -s -i "http://localhost:${PORT}/canary/")
echo "Stable mode request: $R1"
check "stable mode routes to stable" 'stable' "$R1"
check "stable mode returns 200" "200" "$R1"

# Enable canary
EN=$(curl -s "http://localhost:${PORT}/admin/enable-canary/")
echo "Enable canary: $EN"
check "canary enabled" '"mode":"canary"' "$EN"

# Send enough requests to trigger rollback (canary always 500s)
# With 40% threshold and window=20, need > 8 errors
# With 50/50 routing, we send 20 requests (roughly 10 go to canary = 50% errors)
echo "Sending 20 requests to trigger rollback..."
for i in $(seq 1 20); do
    curl -s "http://localhost:${PORT}/canary/" > /dev/null
done

# Check that rollback was triggered
STATUS2=$(curl -s "http://localhost:${PORT}/admin/canary-status/")
echo "Status after requests: $STATUS2"
check "mode rolled back to rollback" '"mode": "rollback"' "$STATUS2"

# After rollback, all requests go to stable
R2=$(curl -s -i "http://localhost:${PORT}/canary/")
echo "Post-rollback request: $R2"
check "post-rollback routes to stable only" 'stable' "$R2"
check "post-rollback returns 200" "200" "$R2"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
