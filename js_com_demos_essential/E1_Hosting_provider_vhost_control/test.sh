#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
PORT=8201
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

echo "=== E1: Hosting-provider vhost control ==="
echo ""

# 1. Anchor server is up
OUT=$(curl -sf -H "Host: anchor.internal" http://127.0.0.1:$PORT/health)
check "anchor server reachable" "anchor-ok" "$OUT"

# 2. Tenant site does not exist yet
CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: alice.example.com" http://127.0.0.1:$PORT/)
check "alice.example.com not reachable before enable" "404" "$CODE"

# 3. Enable alice.example.com
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/tenants/enable?alice.example.com")
check "enable alice returns 'enabled'" "enabled" "$OUT"

# 4. Alice's site is now live — no reload happened
OUT=$(curl -sf -H "Host: alice.example.com" http://127.0.0.1:$PORT/)
check "alice.example.com / responds after enable" "Welcome to alice.example.com" "$OUT"

# 5. Alice's health endpoint works
OUT=$(curl -sf -H "Host: alice.example.com" http://127.0.0.1:$PORT/health)
check "alice.example.com /health responds" "ok" "$OUT"

# 6. Enable a second tenant
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/tenants/enable?bob.example.com")
check "enable bob returns 'enabled'" "enabled" "$OUT"

OUT=$(curl -sf -H "Host: bob.example.com" http://127.0.0.1:$PORT/)
check "bob.example.com / responds after enable" "Welcome to bob.example.com" "$OUT"

# 7. Enabling the same tenant twice is idempotent
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/tenants/enable?alice.example.com")
check "duplicate enable is idempotent" "already-enabled" "$OUT"

# 8. List shows both active tenants
OUT=$(curl -sf "http://127.0.0.1:$PORT/tenants/list")
check "list includes alice" "alice.example.com" "$OUT"
check "list includes bob"   "bob.example.com"   "$OUT"

# 9. Disable alice — instant, no reload
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/tenants/disable?alice.example.com")
check "disable alice returns 'disabled'" "disabled" "$OUT"

CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: alice.example.com" http://127.0.0.1:$PORT/)
check "alice.example.com unreachable after disable" "404" "$CODE"

# 10. Bob's site is unaffected
OUT=$(curl -sf -H "Host: bob.example.com" http://127.0.0.1:$PORT/)
check "bob.example.com still up after alice disabled" "Welcome to bob.example.com" "$OUT"

# 11. Disable non-existent tenant returns 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:$PORT/tenants/disable?nobody.example.com")
check "disable unknown tenant returns 404" "404" "$CODE"

# 12. List reflects current state
OUT=$(curl -sf "http://127.0.0.1:$PORT/tenants/list")
check "list after alice disabled: bob present" "bob.example.com"   "$OUT"
if echo "$OUT" | grep -qF "alice.example.com"; then
    echo "FAIL: alice should not appear in list after disable"
    FAIL=$((FAIL+1))
else
    echo "PASS: alice absent from list after disable"
    PASS=$((PASS+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
