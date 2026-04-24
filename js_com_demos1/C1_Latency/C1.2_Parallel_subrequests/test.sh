#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8161
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

RESP=$(curl -s "http://localhost:${PORT}/merged/")
echo "Merged response:"
echo "$RESP"

check "merged flag is true"          '"merged": true'    "$RESP"
check "alpha service present"        '"service": "alpha"' "$RESP"
check "beta service present"         '"service": "beta"'  "$RESP"
check "gamma service present"        '"service": "gamma"' "$RESP"
check "alpha value 42"               '"value": 42'        "$RESP"
check "beta value 100"               '"value": 100'       "$RESP"
check "gamma value 7"                '"value": 7'         "$RESP"
check "total_value is 149"           '"total_value": 149' "$RESP"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
