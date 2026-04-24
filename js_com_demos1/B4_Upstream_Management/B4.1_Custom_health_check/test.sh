#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8141
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

# Test 1: initial status — both peers healthy
OUT=$(curl -s "http://localhost:$PORT/status/")
check "initial status has peers" '"peers"' "$OUT"
check "peer 8142 present" '8142' "$OUT"
check "peer 8143 present" '8143' "$OUT"
check "initial healthy=true" '"healthy": true' "$OUT"

# Test 2: mark peer 8143 as down
curl -s -X POST "http://localhost:$PORT/admin/toggle/?addr=127.0.0.1:8143&healthy=false" > /dev/null
OUT=$(curl -s "http://localhost:$PORT/status/")
check "after toggle, healthy=false appears" '"healthy": false' "$OUT"

# Test 3: mark it back up
curl -s -X POST "http://localhost:$PORT/admin/toggle/?addr=127.0.0.1:8143&healthy=true" > /dev/null
OUT=$(curl -s "http://localhost:$PORT/status/")
check "after recovery, healthy=true" '"healthy": true' "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
