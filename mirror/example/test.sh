#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
PORT=8300
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

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

# --- 1. Spine + connection flow-local across KEEPALIVE -----------------------
# Two requests over ONE connection (curl reuses it for multiple URLs). The
# per-connection counter must read 1 then 2, and the accept-time client value
# must appear on the response — proving accept->request->response linkage AND
# that flow-local persists across requests on the same connection.
OUT=$(curl -s -D - -o /dev/null -H 'X-Mirror-Route: beta' \
        "http://127.0.0.1:$PORT/" "http://127.0.0.1:$PORT/")
REQS=$(echo "$OUT" | grep -i 'x-mirror-conn-reqs:' | grep -oE '[0-9]+' | tr '\n' ' ')
check "per-connection counter increments across keepalive (got: $REQS)" "1 2 " "$REQS "
check "accept-time client present on response (spine linkage)" "x-mirror-client: 127.0.0.1" "$OUT"
check "route header reflects request header (beta)" "x-mirror-route: beta" "$OUT"

# --- 2. Per-request routing (no header -> stable) ----------------------------
OUT=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/")
check "no route header -> stable" "x-mirror-route: stable" "$OUT"

# --- 3. Global table counter spans connections (monotonic) -------------------
T1=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/" | grep -i 'x-mirror-total:' | grep -oE '[0-9]+')
T2=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/" | grep -i 'x-mirror-total:' | grep -oE '[0-9]+')
if [ "$T2" -gt "$T1" ]; then echo "PASS: global table counter is monotonic ($T1 -> $T2)"; PASS=$((PASS+1));
else echo "FAIL: global table counter not monotonic ($T1 -> $T2)"; FAIL=$((FAIL+1)); fi

# --- 4. Per-event capability gate fires --------------------------------------
# app.js tries setResponseHeader() inside onRequestHeaders (illegal) and carries
# the thrown message forward; the gate must have fired.
OUT=$(curl -s -D - -o /dev/null -H 'X-Mirror-Captest: 1' "http://127.0.0.1:$PORT/")
check "capability gate rejects setResponseHeader in onRequestHeaders" \
      "not valid in event 'onRequestHeaders'" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
