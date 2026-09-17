#!/usr/bin/env bash
# S5 — CVE day: withdraw a grant live, and the delegation dies with it.
PORT=8219; DEMO_NAME="S5 CVE day: withdraw a grant"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. before: the tenant and the library it delegated to both reach the vendor =="
A=$(body /api); show "GET /api" "$A"
check "the tenant's own call reaches the vendor"        '"direct":"fetched"'     "$A"
check "the library's call, on the delegated copy, too"  '"viaLibrary":"fetched"' "$A"
check "the socket grant reads"                          '"port":"number"'        "$A"

echo "== 2. CVE day: withdraw the vendor grant, no reload, no redeploy =="
N=$(body "/ops?op=noconfirm&grant=out"); show "without a confirmation" "$N"
check "class X: the confirmation must name the binding" 'ops.withdraw is class X' "$N"
W=$(body "/ops?op=withdraw&grant=out"); show "ops.withdraw('acme','out',{confirm:'acme'})" "$W"
check "the grant is withdrawn, and one delegation was reached" '{"revoked":["out"],"delegated":1}' "$W"
B=$(body /api); show "GET /api" "$B"
check "the tenant's own call is denied"                 '"direct":"denied"'      "$B"
check "the library's copy died with it (the cascade)"   '"viaLibrary":"denied"'  "$B"
check "the socket grant is untouched"                   '"port":"number"'        "$B"
check "both denials are counted as cap.revoked"         '"denials":2'            "$B"

echo "== 3. no posture lifts it: audit still denies =="
body "/ops?op=audit" >/dev/null
C=$(body /api); show "GET /api under audit" "$C"
check "cap.revoked is unconditional: audit denies too"  '"direct":"denied"'      "$C"
body "/ops?op=enforce" >/dev/null

echo "== 4. the tenant ships a fix: the new epoch holds the same dead grant =="
R=$(body "/ops?op=replace"); show "replace" "$R"
check "epoch 1 is live"                                 '"epoch":1'              "$R"
D=$(body /api); show "GET /api on epoch 1" "$D"
check "still denied: a revocation sticks to the binding" '"direct":"denied","viaLibrary":"denied","port":"number","epoch":1' "$D"
RB=$(body "/ops?op=rollback"); show "rollback" "$RB"
E=$(body /api); show "GET /api on epoch 0 again" "$E"
check "a rollback cannot lift it either"                '"direct":"denied"'      "$E"

echo "== 5. what the auditor reads =="
S=$(body "/ops?op=status"); show "status" "$S"
check "withdrawn() names the grant"                     '"withdrawn":["out"]'    "$S"
DOC=$(body "/ops?op=docs"); show "ops.docs('acme') (excerpt)" "$(echo "$DOC" | grep -o 'out[^\\]*REVOKED[^\\]*')"
check "the tenant's manual says so"                     'REVOKED: every use denies as cap.revoked' "$DOC"

echo "== 6. offboarding: the same verb with no grant name =="
O=$(body "/ops?op=offboard"); show "ops.withdraw('acme',{confirm:'acme'})" "$O"
check "every grant, and the delegation count"           '"revoked":["out","s","author"],"delegated":1' "$O"
F=$(body /api); show "GET /api" "$F"
check "nothing the tenant held survives"                '"direct":"denied","viaLibrary":"denied","port":"undefined"' "$F"

demo_end
