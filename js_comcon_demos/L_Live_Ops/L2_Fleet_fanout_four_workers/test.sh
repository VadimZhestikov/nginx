#!/usr/bin/env bash
# L2 — a replace on one worker reaches all four; no request sees a torn state.
PORT=8213; DEMO_NAME="L2 fleet fan-out, four workers"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start
sleep 0.4

sweep() {   # N requests -> "epoch:worker version" lines
    local n=$1 i
    for i in $(seq 1 "$n"); do
        get /g | tr -d '\r' | awk '/^x-epoch:/{e=$2} /^x-worker:/{w=$2} /"version"/{match($0,/"version":"[^"]*"/); v=substr($0,RSTART+11,RLENGTH-12)} END{print e":"w" "v}'
    done
}

echo "== 1. before: every worker serves epoch 0 =="
S0=$(sweep 12); echo "$S0" | sort | uniq -c | sed 's/^/     /'
check "all v1"                                      "" "$(grep -c v2 <<<"$S0" | sed 's/^0$//')"
W0=$(cut -d: -f2 <<<"$S0" | cut -d' ' -f1 | sort -u | wc -l); echo "     (workers seen: $W0)"

echo "== 2. one control request lands on ONE worker and replaces =="
P=$(body "/ctl?op=patch"); show "op=patch" "$P"
check "epoch advanced"                              '"epoch":1' "$P"

echo "== 3. after: every request, on every worker, serves epoch 1 =="
S1=$(sweep 24); echo "$S1" | sort | uniq -c | sed 's/^/     /'
OLD=$(grep -c "v1" <<<"$S1"); NEW=$(grep -c "v2" <<<"$S1")
if [ "$OLD" -eq 0 ] && [ "$NEW" -eq 24 ]; then echo "  PASS: 24/24 requests on epoch 1, none torn"; PASS=$((PASS+1)); else echo "  FAIL: v1=$OLD v2=$NEW"; FAIL=$((FAIL+1)); fi
W1=$(cut -d: -f2 <<<"$S1" | cut -d' ' -f1 | sort -u | wc -l); echo "     (workers seen after: $W1)"
[ "$W1" -ge 2 ] && { echo "  PASS: more than one worker answered"; PASS=$((PASS+1)); } || { echo "  FAIL: only one worker answered ($W1)"; FAIL=$((FAIL+1)); }

echo "== 4. tombstone and revive, fleet-wide =="
body "/ctl?op=remove" >/dev/null
G=$(for i in 1 2 3 4 5 6 7 8; do get /g | head -1 | tr -d '\r'; done | sort | uniq -c); echo "$G" | sed 's/^/     /'
check "every worker answers 410 while removed"     'HTTP/1.1 410' "$G"
absent "no worker still serves 200"                'HTTP/1.1 200' "$G"
body "/ctl?op=revive" >/dev/null
G2=$(for i in 1 2 3 4 5 6 7 8; do get /g | head -1 | tr -d '\r'; done | sort | uniq -c)
check "revived everywhere"                         'HTTP/1.1 200' "$G2"
absent "no 410 left"                               'HTTP/1.1 410' "$G2"

demo_end
