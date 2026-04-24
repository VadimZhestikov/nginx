#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8102
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

# 1. Static server 2 is reachable via Host header
OUT=$(curl -sf -H "Host: static2.local" http://127.0.0.1:$PORT/ping/)
check "static2.local /ping/ reachable" "static2-pong" "$OUT"

# 2. Dynamic host does not exist yet (falls to default — no matching handler → 404)
CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: dynamic.host" http://127.0.0.1:$PORT/api/)
check "dynamic.host not reachable before creation" "404" "$CODE"

# 3. Add the dynamic vhost
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/add/?dynamic.host")
check "add dynamic.host returns created" "created" "$OUT"

# 4. New vhost is now reachable via Host header
OUT=$(curl -sf -H "Host: dynamic.host" http://127.0.0.1:$PORT/api/)
check "dynamic.host /api/ responds after creation" "Hello from dynamic server" "$OUT"

# 5. Health check on the new vhost also works
OUT=$(curl -sf -H "Host: dynamic.host" http://127.0.0.1:$PORT/health/)
check "dynamic.host /health/ returns ok" "ok" "$OUT"

# 6. Adding same name again is idempotent
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/add/?dynamic.host")
check "duplicate add returns already-exists" "already-exists" "$OUT"

# 7. List includes both static and dynamic servers
OUT=$(curl -sf http://127.0.0.1:$PORT/list/)
check "list includes static1.local" "static1.local" "$OUT"
check "list includes dynamic.host" "dynamic.host" "$OUT"

# 8. Remove the dynamic vhost
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/remove/?dynamic.host")
check "remove dynamic.host returns removed" "removed" "$OUT"

# 9. Dynamic host is no longer reachable
CODE=$(curl -s -o /dev/null -w "%{http_code}" -H "Host: dynamic.host" http://127.0.0.1:$PORT/api/)
check "dynamic.host not reachable after removal" "404" "$CODE"

# 10. Remove non-existent returns 404
CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST "http://127.0.0.1:$PORT/remove/?nonexistent.host")
check "remove nonexistent returns 404" "404" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
