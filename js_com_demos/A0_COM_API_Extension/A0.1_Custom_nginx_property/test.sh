#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8099
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

"$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
sleep 0.2

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.3

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF -- "$expected"; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc"
        echo "      expected: $expected"
        echo "      got:      $actual"
        FAIL=$((FAIL+1))
    fi
}

get()  { curl -sf "http://127.0.0.1:$PORT$1"; }
post() { curl -sf -X POST -H 'Content-Type: application/json' -d "$2" \
              "http://127.0.0.1:$PORT$1"; }
code() { curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT$1"; }

# ── 1. nginx.featureFlags is live: plugin loaded via nginx.use() ──────────────
# nginx.use() in master (init-conf) adds nginx.featureFlags to the COM object;
# all workers inherit it via COW fork before accepting any request.
OUT=$(get /flags/)
check "startup: dark-mode present"      '"dark-mode"'         "$OUT"
check "startup: new-checkout present"   '"new-checkout"'      "$OUT"
check "startup: dark-mode false"        '"dark-mode":false'   "$OUT"
check "startup: new-checkout false"     '"new-checkout":false' "$OUT"

# ── 2. /api/ returns 404 while new-checkout flag is false ─────────────────────
for i in 1 2 3; do
    check "new-checkout off: 404 (req $i)" "404" "$(code /api/)"
done

# ── 3. Enable new-checkout via nginx.featureFlags.set ─────────────────────────
OUT=$(post /flags/enable/ '{"flag":"new-checkout"}')
check "after enable: new-checkout true"  '"new-checkout":true'  "$OUT"
check "after enable: dark-mode unchanged" '"dark-mode":false'   "$OUT"

# nginx.shared write is instantly visible to all workers — no sleep needed.

# ── 4. /api/ returns 200 on all 4 workers ─────────────────────────────────────
# Send 80 requests and collect worker PIDs to verify the COM extension is live
# in every worker, not just the one that handled the enable POST.
declare -A api_pids
for i in $(seq 1 80); do
    OUT=$(curl -sf "http://127.0.0.1:$PORT/api/") || {
        FAIL=$((FAIL+1)); echo "FAIL: /api/ req $i → non-200"; continue
    }
    check "/api/ after enable: ok (req $i)" '"ok":true' "$OUT"
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && api_pids["$PID"]=1
done

if [ "${#api_pids[@]}" -ge 4 ]; then
    echo "PASS: nginx.featureFlags visible on all 4 workers (PIDs: ${!api_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: only ${#api_pids[@]} of 4 workers saw nginx.featureFlags"
    FAIL=$((FAIL+1))
fi

# ── 5. Enable dark-mode; both flags now true ──────────────────────────────────
OUT=$(post /flags/enable/ '{"flag":"dark-mode"}')
check "enable dark-mode: dark-mode true"         '"dark-mode":true'    "$OUT"
check "enable dark-mode: new-checkout still true" '"new-checkout":true' "$OUT"

# ── 6. Disable new-checkout; dark-mode stays true ────────────────────────────
OUT=$(post /flags/disable/ '{"flag":"new-checkout"}')
check "disable new-checkout: new-checkout false" '"new-checkout":false' "$OUT"
check "disable new-checkout: dark-mode true"     '"dark-mode":true'    "$OUT"

check "new-checkout off again: 404" "404" "$(code /api/)"

# ── 7. Operations are idempotent ──────────────────────────────────────────────
post /flags/enable/  '{"flag":"new-checkout"}' > /dev/null
post /flags/enable/  '{"flag":"new-checkout"}' > /dev/null
check "double enable is idempotent: 200" "200" "$(code /api/)"

post /flags/disable/ '{"flag":"new-checkout"}' > /dev/null
post /flags/disable/ '{"flag":"new-checkout"}' > /dev/null
check "double disable is idempotent: 404" "404" "$(code /api/)"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
