#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8115
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
        echo "FAIL: $desc"
        echo "      expected: $expected"
        echo "      got:      $actual"
        FAIL=$((FAIL+1))
    fi
}

# ── 1. Before add: /api/v2/ is 404 on every worker ──────────────────────────
for i in 1 2 3 4 5; do
    CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/api/v2/)
    check "before add: request $i → 404" "404" "$CODE"
done

# ── 2. Add the route (writes shared-memory flag, visible to all workers) ─────
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/admin/add-route/)
check "add-route: confirmation message" "broadcast" "$OUT"

# No sleep needed: nginx.shared writes are immediately visible to all workers.

# ── 3. After add: /api/v2/ is 200 and returns JSON ──────────────────────────
# Send 80 requests and collect worker PIDs to verify every worker handles it.
declare -A seen_pids
for i in $(seq 1 80); do
    OUT=$(curl -sf http://127.0.0.1:$PORT/api/v2/)
    check "after add: request $i contains ok:true" '"ok":true' "$OUT"
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    if [ -n "$PID" ]; then
        seen_pids["$PID"]=1
    fi
done

UNIQUE=${#seen_pids[@]}
if [ "$UNIQUE" -ge 4 ]; then
    echo "PASS: all 4 workers served /api/v2/ (saw PIDs: ${!seen_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: only $UNIQUE of 4 workers responded (saw PIDs: ${!seen_pids[*]})"
    FAIL=$((FAIL+1))
fi

# ── 4. Status endpoint reflects the live route in each worker ────────────────
declare -A status_pids
for i in $(seq 1 40); do
    OUT=$(curl -sf http://127.0.0.1:$PORT/status/)
    check "status: contains /api/v2/" '/api/v2/' "$OUT"
    PID=$(echo "$OUT" | grep -o '"pid":"[0-9]*"' | grep -o '[0-9]*')
    if [ -n "$PID" ]; then
        status_pids["$PID"]=1
    fi
done

if [ "${#status_pids[@]}" -ge 4 ]; then
    echo "PASS: all 4 workers report /api/v2/ in status (${!status_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: only ${#status_pids[@]} of 4 workers in status poll"
    FAIL=$((FAIL+1))
fi

# ── 5. Remove the route ──────────────────────────────────────────────────────
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/admin/remove-route/)
check "remove-route: confirmation message" "broadcast" "$OUT"

# ── 6. After remove: /api/v2/ is 404 again on every worker ──────────────────
for i in 1 2 3 4 5; do
    CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/api/v2/)
    check "after remove: request $i → 404" "404" "$CODE"
done

# ── 7. Add/remove is idempotent ──────────────────────────────────────────────
curl -sf -X POST http://127.0.0.1:$PORT/admin/add-route/    > /dev/null
curl -sf -X POST http://127.0.0.1:$PORT/admin/add-route/    > /dev/null
CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/api/v2/)
check "double add is idempotent: 200" "200" "$CODE"

curl -sf -X POST http://127.0.0.1:$PORT/admin/remove-route/ > /dev/null
curl -sf -X POST http://127.0.0.1:$PORT/admin/remove-route/ > /dev/null
CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/api/v2/)
check "double remove is idempotent: 404" "404" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
