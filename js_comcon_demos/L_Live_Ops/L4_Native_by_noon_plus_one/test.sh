#!/usr/bin/env bash
# L4 — a live epoch compiled in the master; native on every worker, no reload.
PORT=8221; DEMO_NAME="L4 native by noon plus one"
. "$(dirname "$0")/../../lib/demo.sh"

# the compiled build, as D2 selects it; the interpreter build has no tier to show
JIT="${NGINX_JIT:-$ROOT/objs_jit/nginx}"
if [ ! -x "$JIT" ]; then
    echo "no compiled build at $JIT (configure objs_jit; see t/tools/gate.sh --configure) -- skipped"; exit 0
fi
# a private artifact cache, so nothing compiled by an earlier run answers this one
export QJS_JIT_CACHE="$(cd "$(dirname "$0")" && pwd)/logs/jitcache"
rm -rf "$QJS_JIT_CACHE"; mkdir -p "$QJS_JIT_CACHE"
demo_start "$JIT"

# wait until every worker reports the epoch on the tier asked for
settle() {   # epoch tier -> prints the last two bodies, one per worker, or fails
    local epoch=$1 tier=$2 i w0="" w1=""
    for i in $(seq 1 60); do
        local b; b=$(body /score)
        case "$b" in
            *"\"epoch\":$epoch"*"\"worker\":0"*"\"tier\":\"$tier\""*) w0="$b" ;;
            *"\"epoch\":$epoch"*"\"worker\":1"*"\"tier\":\"$tier\""*) w1="$b" ;;
        esac
        [ -n "$w0" ] && [ -n "$w1" ] && { printf '%s\n%s\n' "$w0" "$w1"; return 0; }
        sleep 0.5
    done
    printf '%s\n%s\n' "$w0" "$w1"; return 1
}

FIRST=$(body /score); show "GET /score, first request" "$FIRST"

echo "== 1. epoch 0: admitted at request time, so it starts interpreted... =="
check "epoch 0 serves at once"                       '"epoch":0'         "$FIRST"
check "...interpreted, with the compile pending at the master" '"tier":"bytecode","compiled":"0/3","via":null,"pending":true' "$FIRST"
S0=$(settle 0 native); show "both workers, once settled" "$S0"
check "worker 0 is native by the master, no reload"  '"worker":0,"tier":"native","compiled":"3/3","via":"master"' "$S0"
check "worker 1 too"                                 '"worker":1,"tier":"native","compiled":"3/3","via":"master"' "$S0"
A0=$(grep -o '"score":[0-9]*' <<<"$FIRST" | head -1); A1=$(grep -o '"score":[0-9]*' <<<"$S0" | head -1)
check "the native tier answers what the interpreter did ($A0)" "$A0" "$A1"

echo "== 2. noon: a live replace; 12:01: native everywhere =="
R=$(body "/ops?op=replace"); show "h.replace(V2)" "$R"
check "epoch 1 is admitted"                          '"epoch":1'         "$R"
S1=$(settle 1 native); show "both workers on epoch 1, once settled" "$S1"
check "worker 0 native on epoch 1"                   '"worker":0,"tier":"native","compiled":"3/3","via":"master"' "$S1"
check "worker 1 native on epoch 1"                   '"worker":1,"tier":"native","compiled":"3/3","via":"master"' "$S1"
B1=$(grep -o '"score":[0-9]*' <<<"$S1" | head -1)
if [ "$B1" != "$A1" ]; then echo "  PASS: epoch 1 answers differently ($B1), as its text does"; PASS=$((PASS+1)); else echo "  FAIL: epoch 1 should answer differently"; FAIL=$((FAIL+1)); fi

echo "== 3. what the master did =="
L=$(grep -E "master aot" logs/error.log | sed 's/^.*js comcon: //'); show "error.log" "$L"
check "one helper per text, spawned by the master"   'helper'            "$L"
check "...it compiled three functions and wrote the index" '3 functions compiled' "$L"
absent "no alert anywhere"                           '[alert]'           "$(cat logs/error.log)"

demo_end
