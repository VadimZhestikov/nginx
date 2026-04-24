#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8107
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.3

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected '$expected', got '$actual')"
        FAIL=$((FAIL+1))
    fi
}
check_code() {
    local desc="$1" expected="$2" actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected HTTP $expected, got $actual)"
        FAIL=$((FAIL+1))
    fi
}

# 1. /stable/ always works
OUT=$(curl -sf http://127.0.0.1:$PORT/stable/)
check "/stable/ is always available" "stable: always available" "$OUT"

# 2. /beta/ is initially disabled → 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/beta/)
check_code "/beta/ returns 404 when flag is off" "404" "$CODE"

# 3. List flags — beta starts false
OUT=$(curl -sf http://127.0.0.1:$PORT/admin/flags/)
check "flags list shows beta=false" "beta=false" "$OUT"

# 4. Toggle beta ON
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/toggle/?beta")
check "toggle returns beta=true" "beta=true" "$OUT"

# 5. /beta/ is now enabled → 200
CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/beta/)
check_code "/beta/ returns 200 when flag is on" "200" "$CODE"

OUT=$(curl -sf http://127.0.0.1:$PORT/beta/)
check "/beta/ body confirms feature is enabled" "beta feature is enabled" "$OUT"

# 6. Toggle beta OFF again
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/toggle/?beta")
check "toggle returns beta=false" "beta=false" "$OUT"

# 7. /beta/ is disabled again → 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/beta/)
check_code "/beta/ returns 404 after toggling off" "404" "$CODE"

# 8. Toggle a different flag (darkMode)
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/toggle/?darkMode")
check "toggle darkMode returns darkMode=true" "darkMode=true" "$OUT"

# 9. Unknown flag returns 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:$PORT/admin/toggle/?ghost")
check_code "unknown flag toggle returns 404" "404" "$CODE"

# 10. /stable/ unaffected throughout
OUT=$(curl -sf http://127.0.0.1:$PORT/stable/)
check "/stable/ still works after all flag operations" "stable: always available" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
