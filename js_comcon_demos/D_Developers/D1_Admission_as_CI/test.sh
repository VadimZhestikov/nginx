#!/usr/bin/env bash
# D1 — admission verdicts: structural gate, test phase, request fields, stable codes.
PORT=8210; DEMO_NAME="D1 admission as CI"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

A=$(body /admit); show "GET /admit" "$A"

echo "== 1. the structural gate, without running the fragment =="
check "a clean fragment is certified"                  '"clean":{"certified":true}'                       "$A"
check "an undeclared free name is refused"             '"freeName":{"certified":false,"code":"E_ADMIT_FREENAME"' "$A"
check "declared, it passes the gate (and reads undefined inside)" '"declared":{"certified":true}'         "$A"
check "dynamic code is refused"                        '"dynCode":{"certified":false,"code":"E_ADMIT_DYNCODE"'   "$A"
check "a non-function is refused"                      '"notFn":{"certified":false,"code":"E_ADMIT_ARG"'         "$A"

echo "== 2. the test phase, inside the compartment =="
check "a passing test admits"                          '"testPass":{"admitted":true,"result":42}'         "$A"
check "a failing test refuses with E_ADMIT_TEST"       '"testFail":{"admitted":false,"code":"E_ADMIT_TEST"' "$A"
check "a test that reaches for the host fails: zero blast radius" '"testReach":{"admitted":false'          "$A"

echo "== 3. the request shape is a contract too =="
check "a real Request field is allowed"                '"fields":{"admitted":true'                        "$A"
check "an invented field is refused at admission"      '"fieldsNo":{"admitted":false'                     "$A"

echo "== 4. the closed set to pin =="
check "refusal codes enumerated"                       '"refusalCodes":["E_ADMIT_'                        "$A"

demo_end
