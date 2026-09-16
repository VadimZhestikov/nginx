#!/usr/bin/env bash
# S1 — one capability, six attenuations; every denial is a stable code.
PORT=8204; DEMO_NAME="S1 attenuation vocabulary"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start
sleep 1.5      # the one-second lease expires

A=$(body /arms)
show "GET /arms" "$A"

echo "== what each attenuation leaves =="
check "raw: scalars readable, the reach edge is null"     '"raw":{"address":"string","port":"number","fd":"number","listener":"null"}' "$A"
check "allow(['port']): only port exists"                 '"allow":{"address":"undefined","port":"number","fd":"undefined"'          "$A"
check "redact(['address']): address hidden, rest stay"    '"redact":{"address":"undefined","port":"number","fd":"number"'           "$A"
check "ttl(1): expired, nothing readable"                 '"leased":{"address":"undefined","port":"undefined","fd":"undefined"'     "$A"
check "revoke(): the name does not exist inside"          '"revoked":{"s":"undefined"}'                                              "$A"
check "uses(2/60s): third exercise denied"                '"metered":["string","string","undefined"]'                                "$A"
check "a stack of three narrows and still works"          '"stacked":{"address":"string","port":"number","fd":"undefined"'          "$A"

echo "== the codes an auditor pins =="
check "the reach gate fired (sock.listener)"              '"sock.listener":'   "$A"
check "the lease fired (cap.expired)"                     '"cap.expired":'     "$A"
check "the budget fired (budget.uses)"                    '"budget.uses":'     "$A"

demo_end
