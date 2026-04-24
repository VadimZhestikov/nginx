#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8174
PASS=0; FAIL=0
cd "$DEMO_DIR"
mkdir -p logs

command -v qjs || { echo 'qjs not found — install quickjs (make -C ../../../quickjs install)'; exit 0; }

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

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

# ── Part 1: qjs offline validation ────────────────────────────────────────
echo "=== qjs offline config validation ==="

echo "-- good-config.json --"
if qjs --std validate-config.js good-config.json; then
    echo "PASS: good config validates OK"
    PASS=$((PASS+1))
else
    echo "FAIL: good config should be valid"
    FAIL=$((FAIL+1))
fi

echo ""
echo "-- bad-config.json --"
BAD_OUTPUT=$(qjs --std validate-config.js bad-config.json 2>&1 || true)
echo "$BAD_OUTPUT"
check "bad config: port out of range"          'out of range'      "$BAD_OUTPUT"
check "bad config: debug wrong type"           'expected boolean'  "$BAD_OUTPUT"
check "bad config: maxConnections missing"     'missing required'  "$BAD_OUTPUT"
check "bad config: logLevel not in enum"       'not one of'        "$BAD_OUTPUT"
check "bad config: timeout out of range"       'out of range'      "$BAD_OUTPUT"

# Verify exit code is non-zero for bad config
if qjs --std validate-config.js bad-config.json 2>/dev/null; then
    echo "FAIL: bad config should exit non-zero"
    FAIL=$((FAIL+1))
else
    echo "PASS: bad config exits non-zero"
    PASS=$((PASS+1))
fi

# ── Part 2: same validation via nginx HTTP endpoint ────────────────────────
echo ""
echo "=== nginx HTTP validation endpoint ==="
"$NGINX" -p . -c nginx.conf
sleep 0.3

# Post good config
GOOD=$(curl -s -X POST -H "Content-Type: application/json" \
    -d @good-config.json "http://localhost:${PORT}/validate/")
echo "Good config via HTTP: $GOOD"
check "HTTP: good config returns ok:true" '"ok": true' "$GOOD"
check "HTTP: good config has empty errors" '"errors": []' "$GOOD"

# Post bad config
BAD=$(curl -s -X POST -H "Content-Type: application/json" \
    -d @bad-config.json "http://localhost:${PORT}/validate/")
echo "Bad config via HTTP: $BAD"
check "HTTP: bad config returns ok:false" '"ok": false' "$BAD"
check "HTTP: bad config has errors array" '"errors": [' "$BAD"
check "HTTP: bad config mentions missing" 'missing required' "$BAD"

# POST invalid JSON
INVALID=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '{broken json}' "http://localhost:${PORT}/validate/")
check "HTTP: invalid JSON body returns error" 'invalid JSON' "$INVALID"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
