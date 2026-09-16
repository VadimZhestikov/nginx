#!/usr/bin/env bash
# P3 — three bounds, three bad tenants, one good neighbour.
PORT=8202; DEMO_NAME="P3 tenant budgets"
. "$(dirname "$0")/../../lib/demo.sh"
demo_start

echo "== one request runs all four tenants =="
B=$(body /budgets)
show "GET /budgets" "$B"

echo "== 1. time: a runaway loop is aborted at its deadline =="
check "the spinner was interrupted"                 '"spin":{"threw":"InternalError: interrupted"' "$B"
MS=$(grep -o '"ms":[0-9]*' <<<"$B" | head -1 | cut -d: -f2)
echo "     (deadline fired after ${MS:-?} ms; the budget was 100)"
[ "${MS:-0}" -lt 1000 ] && { echo "  PASS: well under a second"; PASS=$((PASS+1)); } || { echo "  FAIL: took ${MS} ms"; FAIL=$((FAIL+1)); }

echo "== 2. a burst: the allocation past 1 MB is refused, the compartment survives =="
check "the burster hit its allowance"               '"burst":{"threw":"InternalError: out of memory"' "$B"
check "the next call starts from a clean allowance" '"burstAgain":{"threw":"InternalError: out of memory"' "$B"

echo "== 3. a leak: charged per call, refused past the cap =="
check "the leaker was served until it held too much" '"served":1'                        "$B"
check "then refused with the retained-memory code"  '"code":"E_MEM_RETAINED"'            "$B"
check "the host can read the fragment's account"    '"status":{"retained":'              "$B"

echo "== 4. the neighbour =="
check "the good tenant is still served"             '"good":{"returned":"still served"}' "$B"

demo_end
