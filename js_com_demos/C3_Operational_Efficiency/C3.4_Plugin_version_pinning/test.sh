#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8171
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

# Plugin registry shows v1 loaded at startup
PLUGINS=$(curl -s "http://localhost:${PORT}/plugins/")
echo "Initial plugin registry: $PLUGINS"
check "registry shows auth plugin"    '"name": "acmecorp/auth"' "$PLUGINS"
check "registry shows version 1.0.0"  '"version": "1.0.0"'    "$PLUGINS"
check "registry has loadedAt"         '"loadedAt"'             "$PLUGINS"

# /api/ with valid key (v1 installed)
R1=$(curl -s -i -H "X-Api-Key: demo-key-1" "http://localhost:${PORT}/api/")
echo "v1 with valid key: $R1"
check "v1 returns 200" "200" "$R1"
check "v1 has X-Plugin: auth-v1" "auth-v1" "$R1"
check "v1 response has version 1.0.0" '"version":"1.0.0"' "$R1"

# /api/ with invalid key (should 401)
R2=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:${PORT}/api/")
check "no key returns 401" "401" "$R2"

# Hot-upgrade to v2
LOAD=$(curl -s "http://localhost:${PORT}/admin/load-plugin/?name=acmecorp%2Fauth&version=2.0.0")
echo "Load v2: $LOAD"
check "load v2 succeeds" '"ok":true' "$LOAD"
check "load confirms v2 key" '"loaded":"acmecorp/auth@2.0.0"' "$LOAD"

# /api/ with api-key still works in v2
R3=$(curl -s -i -H "X-Api-Key: demo-key-1" "http://localhost:${PORT}/api/")
echo "v2 with api-key: $R3"
check "v2 api-key returns 200" "200" "$R3"
check "v2 has X-Plugin: auth-v2" "auth-v2" "$R3"
check "v2 reports api-key method" '"method":"api-key"' "$R3"

# v2 also accepts bearer tokens
R4=$(curl -s -i -H "Authorization: Bearer bearer-token-xyz" "http://localhost:${PORT}/api/")
echo "v2 with bearer: $R4"
check "v2 bearer returns 200" "200" "$R4"
check "v2 bearer method is bearer" '"method":"bearer"' "$R4"

# Plugin registry now shows v2
PLUGINS2=$(curl -s "http://localhost:${PORT}/plugins/")
echo "Updated registry: $PLUGINS2"
check "registry updated to v2" '"version": "2.0.0"' "$PLUGINS2"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
