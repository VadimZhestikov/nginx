#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8106
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

# 1. Initial state is v1-stable
OUT=$(curl -sf http://127.0.0.1:$PORT/data/)
check "initial /data/ returns v1-stable" "v1-stable" "$OUT"

# 2. Save snapshot of v1-stable
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/admin/snapshot/)
check "snapshot saved confirmation" "snapshot saved: v1-stable" "$OUT"

# 3. Change to v2-experimental
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/change/?v2-experimental")
check "change returns new version label" "v2-experimental" "$OUT"

# 4. /data/ now returns v2-experimental
OUT=$(curl -sf http://127.0.0.1:$PORT/data/)
check "/data/ returns v2-experimental after change" "v2-experimental" "$OUT"

# 5. Change again to v3-broken
OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/change/?v3-broken")
check "second change to v3-broken succeeds" "v3-broken" "$OUT"

OUT=$(curl -sf http://127.0.0.1:$PORT/data/)
check "/data/ returns v3-broken" "v3-broken" "$OUT"

# 6. Rollback restores to v1-stable
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/admin/rollback/)
check "rollback returns v1-stable label" "rolled back to: v1-stable" "$OUT"

# 7. /data/ is back to v1-stable
OUT=$(curl -sf http://127.0.0.1:$PORT/data/)
check "/data/ returns v1-stable after rollback" "v1-stable" "$OUT"

# 8. Status endpoint shows correct info
OUT=$(curl -sf http://127.0.0.1:$PORT/admin/status/)
check "status shows current v1-stable" "v1-stable" "$OUT"

# 9. Rollback without snapshot returns 409 (after restoring, snap is still set — take a fresh snap then remove manually)
# Actually: since savedSnapshot is still set, rollback again is valid. Test that /data/ stays v1-stable.
OUT=$(curl -sf -X POST http://127.0.0.1:$PORT/admin/rollback/)
OUT2=$(curl -sf http://127.0.0.1:$PORT/data/)
check "second rollback keeps v1-stable" "v1-stable" "$OUT2"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
