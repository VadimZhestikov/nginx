#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8116
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs snapshots

# Remove any leftover snapshots from a previous run
rm -f snapshots/*.json

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

admin() { curl -sf "http://127.0.0.1:$PORT$1"; }
post()  { curl -sf -X POST -H 'Content-Type: application/json' -d "$2" \
               "http://127.0.0.1:$PORT$1"; }
code()  { curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT$1"; }

# ── 1. Plugin loaded: admin API is live on all 4 workers ─────────────────────
# nginx.use() in master context propagates the plugin to all workers via COW.
declare -A admin_pids
for i in $(seq 1 40); do
    OUT=$(admin /admin/worker)
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && admin_pids["$PID"]=1
done
if [ "${#admin_pids[@]}" -ge 4 ]; then
    echo "PASS: plugin loaded on all 4 workers (PIDs: ${!admin_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: plugin seen on only ${#admin_pids[@]} of 4 workers"
    FAIL=$((FAIL+1))
fi

# ── 2. Initial state: all flags at default '0' ────────────────────────────────
OUT=$(admin /admin/state)
check "initial state: routes.products=0"  '"routes.products":"0"' "$OUT"
check "initial state: routes.premium=0"   '"routes.premium":"0"'  "$OUT"
check "initial state: canary.weight=0"    '"canary.weight":"0"'    "$OUT"

# ── 3. App routes return 404 while flags are disabled ────────────────────────
check "products disabled: 404" "404" "$(code /api/products/)"
check "premium  disabled: 404" "404" "$(code /api/premium/)"

# ── 4. Enable products via /admin/set; all workers see it immediately ─────────
post /admin/set '{"key":"routes.products","value":"1"}' > /dev/null
check "set products: state reflects change" \
      '"routes.products":"1"' "$(admin /admin/state)"

# Verify all 4 workers serve 200 — nginx.shared write is instantly cross-worker
declare -A prod_pids
for i in $(seq 1 80); do
    OUT=$(curl -sf "http://127.0.0.1:$PORT/api/products/") || { FAIL=$((FAIL+1)); echo "FAIL: products request $i → non-200"; continue; }
    check "products enabled: request $i has worker field" '"worker"' "$OUT"
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && prod_pids["$PID"]=1
done

if [ "${#prod_pids[@]}" -ge 4 ]; then
    echo "PASS: all 4 workers serve /api/products/ (PIDs: ${!prod_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: only ${#prod_pids[@]} of 4 workers served /api/products/"
    FAIL=$((FAIL+1))
fi
check "premium still disabled: 404" "404" "$(code /api/premium/)"

# ── 5. Create snapshot-1: products=1, premium=0, canary=0 ────────────────────
S1=$(post /admin/snapshots '{"name":"products-only"}')
check "snapshot-1 created" '"id"' "$S1"
ID1=$(echo "$S1" | grep -o '"id":"[^"]*"' | grep -o '"[^"]*"$' | tr -d '"')
check "snapshot-1 id format" '-products-only' "$ID1"

# ── 6. Enable premium + set canary weight ────────────────────────────────────
post /admin/set '{"key":"routes.premium","value":"1"}' > /dev/null
post /admin/set '{"key":"canary.weight","value":"50"}' > /dev/null
check "set premium: state reflects change" \
      '"routes.premium":"1"' "$(admin /admin/state)"
check "set canary: state reflects change" \
      '"canary.weight":"50"' "$(admin /admin/state)"
check "premium enabled: 200" "200" "$(code /api/premium/)"

# ── 7. Create snapshot-2: products=1, premium=1, canary=50 ───────────────────
S2=$(post /admin/snapshots '{"name":"full-features"}')
check "snapshot-2 created" '"id"' "$S2"
ID2=$(echo "$S2" | grep -o '"id":"[^"]*"' | grep -o '"[^"]*"$' | tr -d '"')
check "snapshot-2 id format" '-full-features' "$ID2"

# ── 8. List snapshots — two entries ──────────────────────────────────────────
LIST=$(admin /admin/snapshots)
check "list snapshots: contains id1" "$ID1" "$LIST"
check "list snapshots: contains id2" "$ID2" "$LIST"

# ── 9. Rollback to snapshot-1: premium → 0, canary → 0, products stays 1 ────
RB1=$(post /admin/rollback '')
check "rollback-1 result" "rolledBackTo" "$RB1"
check "rollback-1 targets id1" "$ID1" "$RB1"

# All 4 workers must reflect the rollback (nginx.shared = instant cross-worker)
declare -A rb1_pids
for i in $(seq 1 40); do
    OUT=$(admin /status/)
    check "after rollback-1: products still 1" '"routes.products":"1"' "$OUT"
    check "after rollback-1: premium back to 0" '"routes.premium":"0"' "$OUT"
    check "after rollback-1: canary back to 0"  '"canary.weight":"0"'  "$OUT"
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && rb1_pids["$PID"]=1
done

if [ "${#rb1_pids[@]}" -ge 4 ]; then
    echo "PASS: rollback-1 visible on all 4 workers (PIDs: ${!rb1_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: rollback-1 seen on only ${#rb1_pids[@]} of 4 workers"
    FAIL=$((FAIL+1))
fi

check "after rollback-1: products 200" "200" "$(code /api/products/)"
check "after rollback-1: premium 404" "404" "$(code /api/premium/)"

# ── 10. Rollback to base: all flags → '0' ─────────────────────────────────────
RB2=$(post /admin/rollback '')
check "rollback-2 result" "rolledBackTo" "$RB2"
check "rollback-2 targets base" '"base"' "$RB2"

STATE=$(admin /admin/state)
check "after rollback-2: products=0" '"routes.products":"0"' "$STATE"
check "after rollback-2: premium=0"  '"routes.premium":"0"'  "$STATE"
check "after rollback-2: canary=0"   '"canary.weight":"0"'   "$STATE"
check "after rollback-2: products 404" "404" "$(code /api/products/)"

# ── 11. Re-apply snapshot-2 directly: full features back ─────────────────────
APP=$(post "/admin/apply/$ID2" '')
check "apply snapshot-2" '"applied"' "$APP"
STATE2=$(admin /admin/state)
check "after apply id2: products=1" '"routes.products":"1"' "$STATE2"
check "after apply id2: premium=1"  '"routes.premium":"1"'  "$STATE2"
check "after apply id2: canary=50"  '"canary.weight":"50"'  "$STATE2"
check "after apply id2: products 200" "200" "$(code /api/products/)"
check "after apply id2: premium  200" "200" "$(code /api/premium/)"

# ── 12. Snapshot content readable ────────────────────────────────────────────
SNAP_CONTENT=$(admin "/admin/snapshots/$ID1")
check "snapshot content: has ops array" '"ops"' "$SNAP_CONTENT"
check "snapshot content: has shared key" '"shared"' "$SNAP_CONTENT"

# ── 13. Canary endpoint returns v1 or v2 ────────────────────────────────────
OUT=$(curl -sf "http://127.0.0.1:$PORT/api/")
check "canary: has version field" '"version"' "$OUT"
check "canary: has canary field"  '"canary"'  "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
