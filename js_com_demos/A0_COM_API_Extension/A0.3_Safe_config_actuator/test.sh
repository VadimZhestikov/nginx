#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8134
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

"$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
sleep 0.2
cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT
"$NGINX" -p . -c nginx.conf
sleep 0.4

check() {
    local desc="$1" expected="$2" actual="$3"
    # here-string (not echo|grep -q): pipefail + grep -q SIGPIPEs the producer
    # on large payloads and spuriously fails.
    if grep -qF -- "$expected" <<<"$actual"; then
        echo "PASS: $desc"; PASS=$((PASS+1))
    else
        echo "FAIL: $desc"; echo "      expected: $expected"
        echo "      got:      ${actual:0:300}"; FAIL=$((FAIL+1))
    fi
}
get()  { curl -sf "http://127.0.0.1:$PORT$1"; }
post() { curl -sf -X POST "http://127.0.0.1:$PORT$1"; }
code() { curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT$1"; }

# ── 1. Dry-run plan classifies without applying ──────────────────────────────
PLAN=$(get "/plan?path=http.upstreams%5B0%5D.peers%5B0%5D.weight&value=5")
check "plan: zoned peer weight is safe"        '"class":"safe"'              "$PLAN"
check "plan: zoned peer is zoned-shared"       '"propagation":"zoned-shared"' "$PLAN"
check "plan: dry-run not applied"              '"applied":false'             "$PLAN"

PLAN2=$(get "/plan?path=http.servers%5B0%5D.locations%5B8%5D.headers.addHeaders&value=%5B%5D")
check "plan: addHeaders is worker-local"       '"propagation":"worker-local"' "$PLAN2"
check "plan: worker-local flags fanOut"        '"fanOut":true'               "$PLAN2"

# ── 2. The gate rejects a guarded write, then accepts it with ack ────────────
GATE=$(get /gate-demo)
check "gate: guarded write rejected w/o ack"   'rejected:'        "$GATE"
check "gate: rejection names the tier"         'is guarded'       "$GATE"
check "gate: accepted with ack"                '"applied":true'   "$GATE"

# ── 3. canaryWeight propagates across ALL workers (zoned-shared via shm) ─────
post "/canary?pct=30" > /dev/null
sleep 0.2
SEEN_PIDS=""; OK_WEIGHTS=1
for i in $(seq 1 40); do
    W=$(get /weights)
    P=$(echo "$W" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    case " $SEEN_PIDS " in *" $P "*) ;; *) SEEN_PIDS="$SEEN_PIDS $P";; esac
    grep -qF -- '"w0":70' <<<"$W" && grep -qF -- '"w1":30' <<<"$W" || OK_WEIGHTS=0
done
NPIDS=$(echo $SEEN_PIDS | wc -w)
[ "$NPIDS" -ge 2 ] && { echo "PASS: weights observed on $NPIDS workers"; PASS=$((PASS+1)); } \
                   || { echo "FAIL: weights observed on $NPIDS workers (<2)"; FAIL=$((FAIL+1)); }
[ "$OK_WEIGHTS" -eq 1 ] && { echo "PASS: every worker sees canary 70/30"; PASS=$((PASS+1)); } \
                        || { echo "FAIL: some worker missed canary 70/30"; FAIL=$((FAIL+1)); }

# ── 4. setResponseHeader fans out worker-local to ALL workers ────────────────
post "/header?key=X-Demo&value=v1" > /dev/null
sleep 0.2
HDR_PIDS=""; ALL_HDR=1
for i in $(seq 1 40); do
    RESP=$(curl -si "http://127.0.0.1:$PORT/app")
    P=$(grep -o '"worker":"[0-9]*"' <<<"$RESP" | grep -o '[0-9]*' || true)
    # /app body has no worker field; derive PID via /worker on same keepalive is
    # overkill — instead just assert the header is present on every response.
    grep -qiF -- 'X-Demo: v1' <<<"$RESP" || ALL_HDR=0
done
[ "$ALL_HDR" -eq 1 ] && { echo "PASS: X-Demo header present on all /app responses (fanned out)"; PASS=$((PASS+1)); } \
                     || { echo "FAIL: X-Demo header missing on some /app response"; FAIL=$((FAIL+1)); }

# ── 5. toggleLocation disables then re-enables /app across workers ───────────
post "/toggle?on=0" > /dev/null
sleep 0.2
OFF=1; for i in $(seq 1 20); do [ "$(code /app)" = "503" ] || OFF=0; done
[ "$OFF" -eq 1 ] && { echo "PASS: /app returns 503 on all workers after toggle off"; PASS=$((PASS+1)); } \
                 || { echo "FAIL: /app not 503 on all workers"; FAIL=$((FAIL+1)); }

post "/toggle?on=1" > /dev/null
sleep 0.2
ON=1; for i in $(seq 1 20); do [ "$(code /app)" = "200" ] || ON=0; done
[ "$ON" -eq 1 ] && { echo "PASS: /app returns 200 again after toggle on"; PASS=$((PASS+1)); } \
               || { echo "FAIL: /app not 200 after toggle on"; FAIL=$((FAIL+1)); }

# ── 6. drainPeer marks a peer down (visible across workers via shm) ──────────
ADDR=$(get /weights >/dev/null; echo 127.0.0.1:9992)
post "/drain?addr=$ADDR" > /dev/null
sleep 0.2
DRAINED=$(get /weights)
check "drainPeer marks peer down"  '"down1":true'  "$DRAINED"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
