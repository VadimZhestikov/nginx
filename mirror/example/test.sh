#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
PORT=8300
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs
: > logs/error.log

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.3

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qiF "$expected"; then
        echo "PASS: $desc"; PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected '$expected' in: $(echo "$actual" | tr '\n' '|'))"; FAIL=$((FAIL+1))
    fi
}

# --- 1. Spine + REAL per-connection ctx across KEEPALIVE ---------------------
# Two requests over ONE connection (curl reuses it for multiple URLs). The
# per-connection counter must read 1 then 2, and the per-connection SERIAL must
# be identical on both (proving both requests see the SAME per-connection object
# — pilgrim's conn.ctx / r.connCtx, not a per-request object), and the
# accept-time client value must appear (accept->request->response linkage).
OUT=$(curl -s -D - -o /dev/null -H 'X-Mirror-Route: beta' \
        "http://127.0.0.1:$PORT/" "http://127.0.0.1:$PORT/")
REQS=$(echo "$OUT" | grep -i 'x-mirror-conn-reqs:'   | grep -oE '[0-9]+' | tr '\n' ' ')
SER=$(echo "$OUT"  | grep -i 'x-mirror-conn-serial:' | grep -oE '[0-9]+' | tr '\n' ' ')
S1=$(echo "$SER" | awk '{print $1}'); S2=$(echo "$SER" | awk '{print $2}')
check "per-connection counter increments across keepalive (got: $REQS)" "1 2 " "$REQS "
if [ -n "$S1" ] && [ "$S1" = "$S2" ]; then
    echo "PASS: same per-connection object across keepalive (serial $S1 == $S2)"; PASS=$((PASS+1))
else
    echo "FAIL: per-connection serial differs across keepalive ($S1 vs $S2)"; FAIL=$((FAIL+1))
fi
check "accept-time client present on response (spine linkage)" "x-mirror-client: 127.0.0.1" "$OUT"
check "route header reflects request header (beta)" "x-mirror-route: beta" "$OUT"

# --- 2. Distinct connections get distinct serials ----------------------------
A=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/" | grep -i 'x-mirror-conn-serial:' | grep -oE '[0-9]+')
B=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/" | grep -i 'x-mirror-conn-serial:' | grep -oE '[0-9]+')
if [ -n "$A" ] && [ "$A" != "$B" ]; then
    echo "PASS: distinct connections get distinct serials ($A != $B)"; PASS=$((PASS+1))
else
    echo "FAIL: distinct connections did not get distinct serials ($A, $B)"; FAIL=$((FAIL+1))
fi

# --- 3. Per-request routing (no header -> stable) ----------------------------
OUT=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/")
check "no route header -> stable" "x-mirror-route: stable" "$OUT"

# --- 4. Per-event capability gate fires --------------------------------------
OUT=$(curl -s -D - -o /dev/null -H 'X-Mirror-Captest: 1' "http://127.0.0.1:$PORT/")
check "capability gate rejects setResponseHeader in onRequestHeaders" \
      "not valid in event 'onRequestHeaders'" "$OUT"

# --- 5. onClientClose fires (pilgrim conn.onClose), flow-local survives ------
# Several Connection: close requests each open+close a connection -> the close
# hook fires per connection and logs the serial/reqs/client stashed at accept.
for i in 1 2 3; do curl -s -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/"; done
sleep 0.6
CLOSES=$(grep -c 'mirror: onClientClose' logs/error.log || true)
if [ "${CLOSES:-0}" -ge 3 ]; then
    echo "PASS: onClientClose fired on connection close (${CLOSES} events logged)"; PASS=$((PASS+1))
else
    echo "FAIL: onClientClose did not fire enough (${CLOSES} events)"; FAIL=$((FAIL+1))
fi
check "close hook sees accept-time flow-local (client)" "client=127.0.0.1" "$(cat logs/error.log)"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
