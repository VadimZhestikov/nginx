#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8165
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

# /status/ — first request
R1=$(curl -s -i "http://localhost:${PORT}/status/")
echo "=== /status/ first request ==="
echo "$R1"
check "status has X-Request-Id header"   "X-Request-Id: req-1" "$R1"
check "status has X-Duration header"     "X-Duration:"         "$R1"
check "status body has requestId"        '"requestId"'         "$R1"
check "status body hookRan is true"      '"hookRan": true'     "$R1"
check "status body handlerRan is true"   '"handlerRan": true'  "$R1"
check "status body has startTime"        '"startTime"'         "$R1"
check "status body has duration"         '"duration"'          "$R1"

# /status/ — second request gets a different ID
R2=$(curl -s -i "http://localhost:${PORT}/status/")
check "second request gets req-2"        "X-Request-Id: req-2" "$R2"

# /echo/ — r.ctx survives async await
E1=$(curl -s -i "http://localhost:${PORT}/echo/")
echo ""
echo "=== /echo/ async ctx test ==="
echo "$E1"
check "echo X-Ctx-Phase is after-await"  "X-Ctx-Phase: after-await" "$E1"
check "echo body phase is after-await"   '"phase": "after-await"'   "$E1"
check "echo body shows ctx survived"     'persisted through async'  "$E1"
check "echo has X-Ctx-Elapsed header"    "X-Ctx-Elapsed:"           "$E1"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
