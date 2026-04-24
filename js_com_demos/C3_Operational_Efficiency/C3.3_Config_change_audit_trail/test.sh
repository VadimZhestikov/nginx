#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8170
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

# Initially empty audit log
LOG0=$(curl -s "http://localhost:${PORT}/api/audit/")
echo "Initial audit log: $LOG0"
check "initial log is empty" '"count": 0' "$LOG0"

# Record first change
R1=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"action":"add_location","user":"alice","detail":"/beta/"}' \
    "http://localhost:${PORT}/admin/change/")
echo "First change: $R1"
check "change returns 201 seq" '"seq":1' "$R1"
check "change echoes action" '"action":"add_location"' "$R1"
check "change echoes user" '"user":"alice"' "$R1"

# Record second change
R2=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{"action":"update_upstream","user":"bob","detail":"pool_size=50"}' \
    "http://localhost:${PORT}/admin/change/")
echo "Second change: $R2"
check "second change has seq 2" '"seq":2' "$R2"

# Record third change
curl -s -X POST -H "Content-Type: application/json" \
    -d '{"action":"remove_location","user":"alice","detail":"/old/"}' \
    "http://localhost:${PORT}/admin/change/" > /dev/null

# Retrieve audit log
LOG=$(curl -s "http://localhost:${PORT}/api/audit/")
echo "Audit log:"
echo "$LOG"
check "audit log has 3 entries" '"count": 3' "$LOG"
check "audit log has add_location action" '"action": "add_location"' "$LOG"
check "audit log has update_upstream action" '"action": "update_upstream"' "$LOG"
check "audit log has alice as user" '"user": "alice"' "$LOG"
check "audit log has /beta/ detail" '"/beta/"' "$LOG"
check "audit log has timestamp" '"timestamp"' "$LOG"

# Wrong method
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    "http://localhost:${PORT}/admin/change/")
check "GET on /admin/change/ returns 405" "405" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
