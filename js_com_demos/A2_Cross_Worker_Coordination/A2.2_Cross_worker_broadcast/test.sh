#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8109
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

# 1. Initial config contains seeded values
OUT=$(curl -sf http://127.0.0.1:$PORT/config/)
check "initial config has upstream.url" "upstream.url=http://backend-v1.internal/" "$OUT"
check "initial config has feature.theme" "feature.theme=light" "$OUT"
check "initial config has max.rps" "max.rps=1000" "$OUT"

# 2. Update upstream.url
OUT=$(curl -sf -X POST --data "upstream.url=http://backend-v2.internal/" http://127.0.0.1:$PORT/admin/update/)
check "update returns confirmation" "updated: upstream.url=http://backend-v2.internal/" "$OUT"

# 3. New value visible from any worker (send multiple requests to hit both workers)
for i in 1 2 3 4; do
    OUT=$(curl -sf http://127.0.0.1:$PORT/config/)
    if echo "$OUT" | grep -qF "upstream.url=http://backend-v2.internal/"; then
        echo "PASS: updated value visible on request $i"
        PASS=$((PASS+1))
        break
    fi
done

# 4. Add a new key
OUT=$(curl -sf -X POST --data "new.key=hello" http://127.0.0.1:$PORT/admin/update/)
check "add new key succeeds" "updated: new.key=hello" "$OUT"

# 5. New key appears in config listing
OUT=$(curl -sf http://127.0.0.1:$PORT/config/)
check "new key appears in config" "new.key=hello" "$OUT"

# 6. Delete a key
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/delete/?new.key")
check "delete returns deleted" "deleted: new.key" "$OUT"

# 7. Deleted key no longer in config
OUT=$(curl -sf http://127.0.0.1:$PORT/config/)
if echo "$OUT" | grep -qF "new.key"; then
    echo "FAIL: deleted key still present"
    FAIL=$((FAIL+1))
else
    echo "PASS: deleted key removed from config"
    PASS=$((PASS+1))
fi

# 8. Delete non-existent key returns not-found
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/delete/?ghost")
check "delete nonexistent returns not-found" "not-found: ghost" "$OUT"

# 9. Worker info endpoint works
OUT=$(curl -sf http://127.0.0.1:$PORT/worker-info/)
check "worker-info returns worker index" "worker=" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
