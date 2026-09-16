#!/usr/bin/env bash
# D2 — compiled == interpreted: the same fragment, two binaries, one answer.
PORT=8211; DEMO_NAME="D2 compiled tier, same answers"
. "$(dirname "$0")/../../lib/demo.sh"

JIT="${NGINX_JIT:-$ROOT/objs_jit/nginx}"
TOK="eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiI0MiJ9.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"

echo "== 1. the interpreter build =="
demo_start "$ROOT/objs/nginx"
I=$(body "/run?token=$TOK"); show "objs/nginx" "$I"
check "the fragments are NOT compiled on this build"   '"token":{"jit":false'             "$I"
check "the token check answered"                        '"parts":3'                       "$I"
check "the byte scan counted the control character"    '"bad":1'                         "$I"
check "the deadline fired on the interpreter"          '"spin":"interrupted"'            "$I"
IA=$(grep -o '"answer":{.*},"gate"' <<<"$I")
demo_stop

if [ ! -x "$JIT" ]; then
    echo "== 2. no compiled build at $JIT (configure objs_jit; see t/tools/gate.sh --configure) -- skipped =="
    demo_end
fi

echo "== 2. the compiled build =="
demo_start "$JIT"
C=$(body "/run?token=$TOK"); show "objs_jit/nginx" "$C"
check "the fragments ARE compiled on this build"       '"token":{"jit":true'              "$C"
check "the metered loop is compiled too"               '"spin":{"jit":true'               "$C"
check "the deadline fired inside lowered C"            '"spin":"interrupted"'             "$C"
CA=$(grep -o '"answer":{.*},"gate"' <<<"$C")

echo "== 3. SR-2 in miniature: the answers are identical =="
if [ -n "$IA" ] && [ "$IA" = "$CA" ]; then
    echo "  PASS: compiled answer == interpreted answer"; PASS=$((PASS+1))
else
    echo "  FAIL: answers differ"; echo "    interp: $IA"; echo "    jit:    $CA"; FAIL=$((FAIL+1))
fi

demo_end
