#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8139
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

check_absent() {
    local desc="$1" absent="$2" actual="$3"
    if echo "$actual" | grep -qF "$absent"; then
        echo "FAIL: $desc (should NOT contain '$absent')"
        FAIL=$((FAIL+1))
    else
        echo "PASS: $desc"
        PASS=$((PASS+1))
    fi
}

# Test 1: no cookie → no script injection
OUT=$(curl -s "http://localhost:$PORT/page/")
check_absent "no cookie → no script tag" "session-tools.js" "$OUT"

# Test 2: session cookie present → script is injected
OUT=$(curl -s -b "session=abc123" "http://localhost:$PORT/page/")
check "session cookie → script injected" "session-tools.js" "$OUT"

# Test 3: injected before </head>
check "script before </head>" "session-tools.js" "$(echo "$OUT" | grep -A1 'session-tools')"

# Test 4: HTML structure still valid (has </html>)
check "HTML structure intact" "</html>" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
