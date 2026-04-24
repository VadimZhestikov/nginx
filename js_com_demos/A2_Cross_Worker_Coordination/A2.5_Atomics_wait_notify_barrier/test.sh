#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8114
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

cleanup() {
    # Signal the SharedWorker to exit its Atomics.wait loop BEFORE stopping
    # nginx — without this the SW pthread blocks in futex_wait and the master
    # process hangs even after nginx -s stop.
    curl -sf "http://127.0.0.1:$PORT/stop-sw/" >/dev/null 2>&1 || true
    sleep 0.2
    "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
}
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

# Known fibonacci values
# fib(0)=0, fib(1)=1, fib(10)=55, fib(20)=6765, fib(30)=832040

# 1. fib(0) = 0
OUT=$(curl -sf "http://127.0.0.1:$PORT/compute/?0")
check "fib(0) = 0" "fib(0)=0" "$OUT"

# 2. fib(1) = 1
OUT=$(curl -sf "http://127.0.0.1:$PORT/compute/?1")
check "fib(1) = 1" "fib(1)=1" "$OUT"

# 3. fib(10) = 55
OUT=$(curl -sf "http://127.0.0.1:$PORT/compute/?10")
check "fib(10) = 55" "fib(10)=55" "$OUT"

# 4. fib(20) = 6765
OUT=$(curl -sf "http://127.0.0.1:$PORT/compute/?20")
check "fib(20) = 6765" "fib(20)=6765" "$OUT"

# 5. fib(30) = 832040
OUT=$(curl -sf "http://127.0.0.1:$PORT/compute/?30")
check "fib(30) = 832040" "fib(30)=832040" "$OUT"

# 6. Status shows last computation
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows last computation" "last: fib(30)=832040" "$OUT"

# 7. Invalid input
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/compute/?50")
check "n > 40 returns 400" "400" "$CODE"

# 8. Negative input
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/compute/?-1")
check "negative n returns 400" "400" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
