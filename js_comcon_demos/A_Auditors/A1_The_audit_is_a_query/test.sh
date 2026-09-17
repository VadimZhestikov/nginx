#!/usr/bin/env bash
# A1 — the audit is a query, not an interview.
PORT=8215; DEMO_NAME="A1 the audit is a query"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== 1. the trust report, from the resources the session was handed =="
R=$(body /report); show "GET /report" "$R"
check "a session given nothing has only describe()"     '"bareVerbs":["describe"]'         "$R"
check "the verbs decompose over the resources passed in" '"verbs":["bindings","denials","describe","enforce","learn","learnMode","rebind","register","remove","revive","rewrite","rollback","shadow","snapshot","trustReport"]' "$R"
check "the registered binding is in the report"        '"name":"vendor"'                  "$R"
check "the enforced-by table names each contract field" '"enforcedBy":[{"field":'         "$R"
check "denials are counted by gate (sock.listener)"    '"sock.listener":1'                "$R"
check "the two resources with no host spelling are named, not hidden" '"noHost":["provenance","signing"]' "$R"
check "what the library does not enforce is listed as absent" 'postures'                  "$R"

echo "== 2. where is fetch used? from bytecode, nothing run =="
W=$(body "/where?name=fetch"); show "GET /where?name=fetch" "$W"
check "two references, both calls"                     '"references":2'                   "$W"
check "two call sites, the nested argument attributed" '"callsites":[{"line":'            "$W"
check "the functions of the module are enumerable"     '"functions":["real"]'             "$W"
check "reads of the tree are quotations, not source strings" '"readsAreQuotations":true'  "$W"
check "the binding is redacted by default"             '"bindingRedacted":true'           "$W"

echo "== 3. the call no grant can name: rewrite it, review it, install it, undo it =="
V0=$(body /v); show "GET /v before" "$V0"
check "the vendor code ran its local alias"            'AB|RAN:a,RAN:b'                   "$V0"
H=$(body "/rewrite?op=harden"); show "op=harden" "$H"
check "both sites rewritten"                           '"sitesRewritten":2'               "$H"
V1=$(body /v); show "GET /v after" "$V1"
check "the guard answered; the wrapped function never ran" 'XX|'                          "$V1"
absent "no RAN: marker"                                'RAN:'                             "$V1"
body "/rewrite?op=rollback" >/dev/null
V2=$(body /v)
check "rollback: the original serves again"            'AB|RAN:a,RAN:b'                   "$V2"

demo_end
