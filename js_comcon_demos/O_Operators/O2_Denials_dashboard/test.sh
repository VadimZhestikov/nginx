#!/usr/bin/env bash
# O2 — the codes, the counters, and the switch.
PORT=8209; DEMO_NAME="O2 denials dashboard"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start
sleep 1.5      # the probe's one-second lease expires

echo "== 1. the dashboard: two closed sets of codes =="
D=$(body /dash); show "GET /dash" "$D"
check "the fleet starts in enforce"                    '"mode":"enforce"'            "$D"
check "denial codes are enumerable"                    '"denialCodes":["sock.listener"' "$D"
check "refusal codes are enumerable"                   '"refusalCodes":['            "$D"
check "E_ADMIT_FREENAME is among them"                 'E_ADMIT_FREENAME'            "$D"
check "E_MEM_RETAINED is among them"                   'E_MEM_RETAINED'              "$D"

echo "== 2. enforce: the gate denies, and counts =="
P1=$(body /probe); show "GET /probe" "$P1"
check "denied under enforce"                           '"result":"denied"'           "$P1"
check "counted once"                                   '"counted":1'                 "$P1"

echo "== 3. audit: the same gate logs and ALLOWS, and still counts =="
M=$(body "/mode?set=audit"); show "mode=audit" "$M"
check "the switch took"                                '"mode":"audit"'              "$M"
P2=$(body /probe); show "GET /probe" "$P2"
check "allowed under audit"                            '"result":"allowed'           "$P2"
check "and still counted"                              '"counted":1'                 "$P2"

echo "== 4. back to enforce; a posture word nobody enforces is refused =="
M2=$(body "/mode?set=enforce"); check "back to enforce" '"mode":"enforce"'          "$M2"
M3=$(body "/mode?set=maybe");   show "mode=maybe" "$M3"
check "an unknown mode is refused, not read as something" '"refused":'              "$M3"
check "the mode did not change"                        '"mode":"enforce"'            "$M3"

echo "== 5. the dashboard after =="
D2=$(body /dash); show "GET /dash" "$D2"
check "cap.expired shows in the fired set"             '"fired":{"cap.expired":2}'    "$D2"

demo_end
