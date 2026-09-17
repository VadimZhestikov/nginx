#!/usr/bin/env bash
# L1 — a live binding is a sequence of epochs; the fix is one more of them.
PORT=8212; DEMO_NAME="L1 patch at noon: epochs and rollback"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. epoch 0 serves, with the bug =="
R0=$(get "/price?qty=12"); show "GET /price?qty=12" "$R0"
check "epoch 0"                                  'x-epoch: 0'          "$R0"
check "v1: the discount is computed"             '"discount":0.1'      "$R0"
check "v1: but never applied (3000)"             '"total":3000'        "$R0"

echo "== 2. the noon fix: admit the new text, swap it in as epoch 1 =="
P=$(body "/ctl?op=patch"); show "op=patch" "$P"
check "epoch advanced to 1"                      '"epoch":1'           "$P"
R1=$(get "/price?qty=12"); show "GET /price?qty=12" "$R1"
check "epoch 1 serves"                           'x-epoch: 1'          "$R1"
check "v2: the discount is applied (2700)"       '"total":2700'        "$R1"

echo "== 3. bad fix? one call back =="
B=$(body "/ctl?op=rollback"); show "op=rollback" "$B"
R2=$(get "/price?qty=12")
check "rollback: the previous epoch serves again" '"total":3000'       "$R2"

echo "== 4. tombstone and revive =="
body "/ctl?op=remove" >/dev/null
R3=$(get "/price?qty=1"); show "after remove" "$R3"
check "the site answers 410 while removed"       'HTTP/1.1 410'        "$R3"
body "/ctl?op=revive" >/dev/null
R4=$(get "/price?qty=1")
check "revived: 200 again"                       'HTTP/1.1 200'        "$R4"

echo "== 5. what the handle can do, and what replacing costs =="
D=$(body "/ctl?op=describe"); show "op=describe" "$D"
check "the mutation ops are listed with safety classes" '"ops":'       "$D"
S=$(body "/ctl?op=stress"); show "op=stress (300 replacements)" "$S"
DELTA=$(grep -o '"delta":-\?[0-9]*' <<<"$S" | cut -d: -f2)
if [ -n "$DELTA" ] && [ "$DELTA" -lt 262144 ]; then echo "  PASS: heap flat after 300 replacements (delta ${DELTA} bytes)"; PASS=$((PASS+1)); else echo "  FAIL: heap grew by ${DELTA:-?} bytes"; FAIL=$((FAIL+1)); fi
T=$(body "/ctl?op=tier"); show "op=tier" "$T"
check "the tier of the live epoch is reported, never assumed" '"aot":{"jit":' "$T"

demo_end
