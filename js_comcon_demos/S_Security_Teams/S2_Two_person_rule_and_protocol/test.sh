#!/usr/bin/env bash
# S2 — the two-person rule, and enforced operation order.
PORT=8205; DEMO_NAME="S2 two-person rule and protocol"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. cosign: the attempt is the consent =="
A1=$(body "/attempt?who=alice"); show "alice attempts" "$A1"
check "alice alone is denied"                     '"result":"denied"'      "$A1"
check "nothing reached the host queue"            '"queued":0'             "$A1"
check "the denial has a code (cap.cosign)"        '"cap.cosign":'          "$A1"
A2=$(body "/attempt?who=alice"); show "alice again" "$A2"
check "one principal twice is still one consent" '"queued":0'             "$A2"
B=$(body "/attempt?who=bob"); show "bob attempts the same decision" "$B"
check "bob completes the quorum: the intent is recorded" '"result":"recorded"' "$B"
check "the intent is in bob's queue for the host"  '"queued":1'            "$B"

echo "== 2. protocol: order, not completion =="
P=$(body /protocol); show "GET /protocol" "$P"
check "the declared order is allowed (star repeats)" '"inOrder":["address","port","port","fd"]' "$P"
check "a starred step may happen zero times"         '"skipStar":["address","fd"]'               "$P"
check "fd before address is denied; the cursor does not advance" '"outOfOrder":["-","address"]' "$P"
check "protocol('fd') is a one-shot capability"      '"oneShot":["fd","-","-"]'                  "$P"
check "the denial has a code (cap.protocol)"         '"cap.protocol":'                           "$P"

demo_end
