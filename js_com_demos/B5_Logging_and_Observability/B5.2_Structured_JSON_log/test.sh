#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8149
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

# Make a request
curl -s -A "TestAgent/1.0" "http://localhost:$PORT/api/" > /dev/null

# Give the log a moment to flush
sleep 0.1

LOG=$(cat logs/error.log)

# Test 1: JSON-formatted log entry present
check "access log entry present" '"method"' "$LOG"

# Test 2: URI is logged
check "URI logged" '"/api/"' "$LOG"

# Test 3: method is logged
check "method logged" '"GET"' "$LOG"

# Test 4: user-agent is logged
check "user-agent logged" 'TestAgent' "$LOG"

# Test 5: timestamp present
check "timestamp logged" '"ts"' "$LOG"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
