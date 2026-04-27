#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
API_PORT=8202
OPS_PORT=8203
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

echo "=== E2: DDoS limit_req hot-patch ==="
echo ""

# 1. API responds normally under normal load
OUT=$(curl -sf http://127.0.0.1:$API_PORT/api)
check "API responds in normal mode" "api-ok" "$OUT"

# 2. Inspect initial parameters
OUT=$(curl -sf http://127.0.0.1:$OPS_PORT/ddos/status)
check "initial rate is 20" '"rate": 20'   "$OUT"
check "initial burst is 10" '"burst": 10'  "$OUT"
check "initial nodelay is false" '"nodelay": false' "$OUT"

# 3. Activate DDoS mitigation — instant, no reload
OUT=$(curl -sf -X POST http://127.0.0.1:$OPS_PORT/ddos/tighten)
check "tighten returns 'mitigation activated'" "mitigation activated" "$OUT"

# 4. Parameters changed in the live process
OUT=$(curl -sf http://127.0.0.1:$OPS_PORT/ddos/status)
check "rate tightened to 2"     '"rate": 2'    "$OUT"
check "burst tightened to 2"    '"burst": 2'   "$OUT"
check "nodelay set to true"     '"nodelay": true' "$OUT"
check "dryRun is false"         '"dryRun": false' "$OUT"

# 5. API still answers requests that do fit the tight rate
OUT=$(curl -sf http://127.0.0.1:$API_PORT/api)
check "API still responds after tighten" "api-ok" "$OUT"

# 6. Restore normal mode
OUT=$(curl -sf -X POST http://127.0.0.1:$OPS_PORT/ddos/relax)
check "relax returns 'normal mode restored'" "normal mode restored" "$OUT"

# 7. Parameters are back to normal
OUT=$(curl -sf http://127.0.0.1:$OPS_PORT/ddos/status)
check "rate restored to 20"      '"rate": 20'   "$OUT"
check "burst restored to 10"     '"burst": 10'  "$OUT"
check "nodelay cleared to false" '"nodelay": false' "$OUT"

# 8. API continues to work normally after relax
OUT=$(curl -sf http://127.0.0.1:$API_PORT/api)
check "API responds normally after relax" "api-ok" "$OUT"

# 9. Can cycle tighten → relax repeatedly (idempotency / no state leak)
curl -sf -X POST http://127.0.0.1:$OPS_PORT/ddos/tighten > /dev/null
curl -sf -X POST http://127.0.0.1:$OPS_PORT/ddos/relax   > /dev/null
OUT=$(curl -sf http://127.0.0.1:$OPS_PORT/ddos/status)
check "rate correct after second cycle" '"rate": 20' "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
