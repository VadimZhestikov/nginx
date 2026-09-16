#!/usr/bin/env bash
# P2 — observe-first rollout: the fleet in learn, the bindings' own words, the flip.
PORT=8201; DEMO_NAME="P2 observe then enforce"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start
sleep 1.5      # the one-second lease on the socket capability expires

echo "== 1. onboarding: the fleet is in LEARN, and the bindings' own words still win =="
P=$(body /postures)
show "GET /postures" "$P"
check "the fleet is in learn"                            '"fleet":"learn"'                                     "$P"
check "shadow binding (audit): the read is ALLOWED"      '"shadow":{"addressType":"string","portType":"number"' "$P"
check "deny binding: denied, whatever the fleet is in"   '"enforce":{"addressType":"undefined","portType":"undefined"' "$P"
check "a binding with no word follows the fleet: allowed" '"inherit":{"addressType":"string"'                  "$P"
absent "the expiry was counted, not zero"                '"cap.expired":0'                                     "$P"

echo "== 2. a greedy tenant, harvested rather than killed =="
L=$(body /learn)
show "GET /learn" "$L"
check "the tenant ran to completion in learn mode"       '"result":"handled /learn"'                          "$L"
check "its reach for config mutation was recorded"       'nginx.http.addServer'                               "$L"
check "its reach for a raw socket was recorded"          'createSocket'                                       "$L"
check "its reach for network egress was recorded"        'fetch'                                              "$L"

echo "== 3. the environment is settled: flip the fleet to enforce =="
M=$(body "/mode?set=enforce"); show "mode=enforce" "$M"
check "the fleet is in enforce"                          '"fleet":"enforce"'                                   "$M"
P2=$(body /postures)
show "GET /postures" "$P2"
check "the no-word binding now denies"                   '"inherit":{"addressType":"undefined"'                "$P2"
check "the shadow binding is still shadowed"             '"shadow":{"addressType":"string"'                    "$P2"

demo_end
