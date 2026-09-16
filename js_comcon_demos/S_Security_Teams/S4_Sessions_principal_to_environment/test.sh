#!/usr/bin/env bash
# S4 — principal -> environment: narrowing only, data only, revocable.
PORT=8207; DEMO_NAME="S4 sessions: principal to environment"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. a principal that may hold the socket =="
C=$(body "/session?who=ci@acme"); show "ci@acme" "$C"
check "the mapping carried both names"          '"granted":["JSON","s"]'         "$C"
check "the probe holds the socket wrapper"      '"probe":{"sock":"object"'       "$C"

echo "== 2. a principal that may not =="
D=$(body "/session?who=dev@acme"); show "dev@acme" "$D"
check "only JSON was carried"                   '"granted":["JSON"]'             "$D"
check "the probe holds no socket"               '"probe":{"sock":"undefined"'    "$D"

echo "== 3. unknown, and greedy =="
N=$(body "/session?who=nobody@nowhere"); show "nobody@nowhere" "$N"
check "an unknown principal gets an empty env"  '"granted":[]'                   "$N"
G=$(body "/session?who=greedy@acme"); show "greedy@acme" "$G"
check "a mapping naming more than the env holds is REFUSED, not narrowed" '"refused":' "$G"

echo "== 4. revocation is a row removal =="
R=$(body "/revoke?who=ci@acme"); show "revoke ci@acme" "$R"
C2=$(body "/session?who=ci@acme"); show "ci@acme again" "$C2"
check "the next resolve sees nothing"           '"granted":[]'                   "$C2"
check "the probe holds no socket any more"      '"probe":{"sock":"undefined"'    "$C2"

demo_end
