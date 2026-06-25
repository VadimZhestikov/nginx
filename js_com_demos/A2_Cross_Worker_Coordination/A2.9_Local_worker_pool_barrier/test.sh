#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8234
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

cleanup() {
    # Pool workers are parked in their channel loop between jobs (the Atomics
    # barrier lives inside a job and returns), so they are terminable and nginx
    # shuts down cleanly.
    "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
}
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.3

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "PASS: $desc"; PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected '$expected', got '$actual')"; FAIL=$((FAIL+1))
    fi
}

# Parallel sum of 0..n-1 = n*(n-1)/2, computed across the local Worker pool.
# 1. n=10  -> 45
OUT=$(curl -sf "http://127.0.0.1:$PORT/task/?10")
check "n=10 local_sum=45"            '"local_sum":45'         "$OUT"

# 2. n=100 -> 4950
OUT=$(curl -sf "http://127.0.0.1:$PORT/task/?100")
check "n=100 local_sum=4950"         '"local_sum":4950'       "$OUT"

# 3. n=1000 -> 499500
OUT=$(curl -sf "http://127.0.0.1:$PORT/task/?1000")
check "n=1000 local_sum=499500"      '"local_sum":499500'     "$OUT"

# 4. n=40000 -> 799980000 (large; exercises real per-slice work)
OUT=$(curl -sf "http://127.0.0.1:$PORT/task/?40000")
check "n=40000 local_sum=799980000"  '"local_sum":799980000'  "$OUT"

# 5. SharedWorker global aggregate across the 4 tasks above:
#    count = 4, total = 45 + 4950 + 499500 + 799980000 = 800484495
OUT=$(curl -sf "http://127.0.0.1:$PORT/global/")
check "global_count = 4"             '"globalCount":4'         "$OUT"
check "global_total = 800484495"     '"globalTotal":800484495' "$OUT"

# 6. Validation
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/task/?-1")
check "negative n returns 400"       "400" "$CODE"
CODE=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/task/?40001")
check "n > 40000 returns 400"        "400" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
