#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8101
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

# 1. Initial status shows v1 cert
OUT=$(curl -sf http://127.0.0.1:$PORT/status/)
check "initial cert version is v1-initial" "v1-initial" "$OUT"

# 2. Rotate certificate (simulated)
OUT=$(curl -sf -X POST -H "X-Cert-Version: v2-rotated" http://127.0.0.1:$PORT/admin/cert/)
check "rotate responds 200 with confirmation" "Certificate rotated" "$OUT"

# 3. Status now shows new cert version
OUT=$(curl -sf http://127.0.0.1:$PORT/status/)
check "status shows v2-rotated after rotation" "v2-rotated" "$OUT"

# 4. Rotate again to v3
OUT=$(curl -sf -X POST -H "X-Cert-Version: v3-renewed" http://127.0.0.1:$PORT/admin/cert/)
check "second rotation succeeds" "v3-renewed" "$OUT"

# 5. Wrong method is rejected
OUT=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:$PORT/admin/cert/)
check "GET on admin endpoint returns 405" "405" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
