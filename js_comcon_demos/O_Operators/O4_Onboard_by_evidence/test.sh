#!/usr/bin/env bash
# O4 — onboard by evidence: the allow-suite.
PORT=8220; DEMO_NAME="O4 onboard by evidence"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. record what the binding answers =="
R=$(body "/ops?op=record"); show "ops.record('pricing')" "$R"
check "recording starts on the live epoch" '"recording":true' "$R"
for q in "sku=widget&qty=1" "sku=widget&qty=12" "sku=gadget&qty=6" "sku=gadget&qty=2" "sku=widget&qty=1" "sku=bolt&qty=-3"; do body "/price?$q" >/dev/null; done
S=$(body "/ops?op=suite"); show "ops.suite('pricing')" "$S"
check "six calls, five distinct inputs, none unstable" '"recorded":6,"distinct":5,"unstable":0' "$S"
check "a case is the input and the answer it got" '{\"sku\":\"widget\",\"qty\":12} -> {\"sku\":\"widget\",\"qty\":12,\"total\":2700}' "$S"

echo "== 2. coverage: what the traffic never reached =="
C=$(body "/ops?op=coverage"); show "ops.coverage('pricing')" "$C"
check "four functions, all entered by this traffic (the refund path included)" '"total":4,"called":4,"percent":100' "$C"

echo "== 3. rehearse next week's drift on the host, nothing bound =="
H=$(body "/ops?op=rehearse"); show "std.suite.check(drift, suite)" "$H"
check "the drifted candidate fails the replay" '"ok":false' "$H"
check "...on exactly the case the moved threshold changes, both answers named" '"input":"{\"sku\":\"gadget\",\"qty\":6}","expected":"{\"sku\":\"gadget\",\"qty\":6,\"total\":5400}","got":"{\"sku\":\"gadget\",\"qty\":6,\"total\":4860}"' "$H"
check "...the four cases it still answers the same are counted" '"passed":4' "$H"

echo "== 4. guard: pin the suite; the drift is refused at admission =="
G=$(body "/ops?op=guard"); show "ops.guard('pricing')" "$G"
check "the suite is now the binding's contract tests" '"tests":true' "$G"
D=$(body "/ops?op=diff"); show "ops.diff('pricing', {imports: []})" "$D"
check "removing the pin would be a widening" '"verdict":"widening"' "$D"
B=$(body "/ops?op=rebind-bad"); show "ops.rebind('pricing', drift)" "$B"
check "refused with the admission code"        '"refused":"E_ADMIT_TEST"' "$B"
check "epoch 0 stays live"                     '"epoch":0'               "$B"
check "the refusal names the case"             'allow-suite case'        "$B"
P=$(body "/price?sku=widget&qty=12"); show "GET /price?sku=widget&qty=12" "$P"
check "the live answer is unchanged"           '"total":2700,"epoch":0'  "$P"

echo "== 5. a rewrite that answers the same goes live =="
OK=$(body "/ops?op=rebind-good"); show "ops.rebind('pricing', same)" "$OK"
check "admitted as epoch 1"                    '"epoch":1'               "$OK"
P2=$(body "/price?sku=widget&qty=12"); show "GET /price?sku=widget&qty=12" "$P2"
check "...and serves the same answer"          '"total":2700,"epoch":1'  "$P2"

demo_end
