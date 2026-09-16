#!/usr/bin/env bash
# P4 — the authoring tier: a tenant admits tenants, and can only narrow.
PORT=8203; DEMO_NAME="P4 reseller authors sub-fragments"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

R=$(body /reseller)
show "GET /reseller" "$R"

echo "== 1. the same pipeline from the other side of the membrane =="
check "the host's limit is visible to the reseller"    '"limit":2'                          "$R"
check "a sub-fragment computes on JSON in, JSON out"    '"sub2":42'                          "$R"
check "the live count went 0, 1, 2"                     '"used2":2'                          "$R"

echo "== 2. copy, then narrow -- never widen =="
check "sub-fragment sees the address, not the port"     '"sub1":{"addr":"string","port":"undefined"}' "$R"
check "widening the mask is refused"                    '"escalate":"E_CAP_ESCALATE"'         "$R"
check "a sub-fragment cannot name its parent's author"  '"namesParent":"E_ADMIT_FREENAME"'    "$R"

echo "== 3. the limit =="
check "a third sub-fragment is over the limit"          '"third":"E_AUTHOR_LIMIT"'            "$R"

demo_end
