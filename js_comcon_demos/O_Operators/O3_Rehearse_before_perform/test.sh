#!/usr/bin/env bash
# O3 — diff, shadow on live traffic, read the would-deny list, then perform.
PORT=8216; DEMO_NAME="O3 rehearse before perform"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. the diff predicts =="
D=$(body /diff); show "GET /diff" "$D"
check "the candidate is a narrowing: auto-safe"          '"candidate":{"verdict":"narrowing","autoSafe":true' "$D"
check "it names the budget that appears"                 '"path":"grants.s.budget"'                        "$D"
check "and the lease and the deadline"                   '"path":"grants.s.ttlSeconds"'                     "$D"
check "the other proposal is a widening: not auto-safe"  '"looser":{"verdict":"widening","autoSafe":false' "$D"
check "it names the request check switched off"          '"path":"checkRequest","from":true,"to":false,"direction":"widening"' "$D"

echo "== 2. shadow confirms, on live traffic =="
for i in 1 2 3 4 5; do body /t >/dev/null; done
T=$(body /t); show "GET /t (the sixth)" "$T"
check "the live binding serves the port"                 '"served":{"seen":["port"]}'                    "$T"
check "the shadowed candidate still serves it: audit logs and ALLOWS" '"shadowed":{"seen":["port"]}'    "$T"
R=$(body /rehearsal); show "GET /rehearsal" "$R"
check "the live binding's own rows: nothing fired"       '"live":{"posture":"deny","fleet":"enforce","observing":false,"total":0,"events":[]}' "$R"
check "the candidate's rows are a would-deny list"       '"candidate":{"posture":"audit","fleet":"enforce","observing":true,"total":4' "$R"
check "four of six requests would have been denied by the budget" '"events":[{"op":"budget.uses","n":4}]' "$R"

echo "== 3. perform: the same text, onViolation deny =="
P=$(body /perform); show "GET /perform" "$P"
check "the budget bites: the read is denied"             '"enforced":{"seen":[]}'                        "$P"
check "and the binding's rows say denied, not observed"  '"posture":"deny","fleet":"enforce","observing":false' "$P"

demo_end
