#!/usr/bin/env bash
# O1 — review, diff, apply (confirmed), rollback; an ill-typed proposal leaves nothing behind.
PORT=8208; DEMO_NAME="O1 config proposal: review / apply / rollback"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. review: typecheck against the policy, nothing applied =="
R=$(body "/ctl?op=review"); show "op=review" "$R"
check "the plan is ok"                               '"ok":true'                    "$R"
check "the plan carries a content hash"              '"hash":"'                     "$R"
check "root is a safe-class op"                      'acme.root = '                 "$R"
ROOT0=$(grep -o '"now":{"root":[^,]*' <<<"$R" | head -1); echo "     (root before: ${ROOT0#*root\":})"
check "the plan reports the current subtree"          '"now":{"root":'               "$R"

echo "== 2. diff: what would change =="
D=$(body "/ctl?op=diff"); show "op=diff" "$D"
check "the diff names the change to root"            '-> \"/srv/acme\"'             "$D"

echo "== 3. apply needs explicit confirmation for a guarded class =="
N=$(body "/ctl?op=apply-noconfirm"); show "op=apply-noconfirm" "$N"
check "refused without confirm"                      '"out":"needs-confirm"'        "$N"
check "still untouched"                              "$ROOT0"                       "$N"

echo "== 4. apply, confirmed; then rollback =="
A=$(body "/ctl?op=apply"); show "op=apply" "$A"
check "applied"                                      '"root":"/srv/acme"'           "$A"
check "the timeout was applied too"                  '"connectTimeout":2500'        "$A"
B=$(body "/ctl?op=rollback"); show "op=rollback" "$B"
check "rollback restored the previous values"        "$ROOT0"                       "$B"

echo "== 5. all-or-nothing: an ill-typed proposal leaves nothing behind =="
X=$(body "/ctl?op=atomic"); show "op=atomic" "$X"
check "the review passed (the type system cannot know upstreams)" '"reviewOk":true' "$X"
check "nginx refused the second op"                  '"out":"refused:'              "$X"
check "the first op was undone: root untouched"      "$ROOT0"                       "$X"

echo "== 6. a function proposal, realized under the operator's env =="
Q=$(body "/ctl?op=realize"); show "op=realize" "$Q"
check "a quotation is an inert frozen description"   '"quoted":"object, frozen"'    "$Q"
check "realized under an empty env, it computes"     '"realized":42'                "$Q"
check "a proposal naming an undeclared free name is refused at realization" '"hidden":"refused:' "$Q"

demo_end
