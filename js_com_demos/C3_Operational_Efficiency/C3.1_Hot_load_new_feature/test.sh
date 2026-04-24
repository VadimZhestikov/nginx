#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8166
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

# Startup — v1 should be active
V=$(curl -s "http://localhost:${PORT}/admin/version/")
echo "Initial version: $V"
check "initial version is v1" '"version":"v1"' "$V"

# /api/ should return v1 response
R1=$(curl -s -i "http://localhost:${PORT}/api/")
echo "v1 API response: $R1"
check "v1 response contains 'v1 response'" "v1 response" "$R1"
check "v1 has X-Plugin-Version: v1"        "v1"           "$R1"

# Hot-swap to v2
LOAD=$(curl -s "http://localhost:${PORT}/admin/load/?version=v2")
echo "Load v2: $LOAD"
check "load response says v2 loaded" "loaded plugin v2" "$LOAD"

# /api/ should now return v2 response — no nginx reload!
R2=$(curl -s -i "http://localhost:${PORT}/api/")
echo "v2 API response: $R2"
check "v2 response contains 'v2 response'" "v2 response"  "$R2"
check "v2 has X-Plugin-Version: v2"        "v2"           "$R2"
check "v2 has X-Feature-Flags header"      "X-Feature-Flags" "$R2"

# Version endpoint confirms v2
V2=$(curl -s "http://localhost:${PORT}/admin/version/")
echo "Current version: $V2"
check "version endpoint reports v2" '"version":"v2"' "$V2"

# Hot-swap back to v1
curl -s "http://localhost:${PORT}/admin/load/?version=v1" > /dev/null
R3=$(curl -s "http://localhost:${PORT}/api/")
check "after rollback to v1, API returns v1" "v1 response" "$R3"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
