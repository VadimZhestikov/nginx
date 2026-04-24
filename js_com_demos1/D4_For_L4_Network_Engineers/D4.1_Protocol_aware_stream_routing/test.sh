#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8190
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

# 1. GET /status/ — routing table is populated
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows routing_table" "routing_table" "$OUT"
check "status shows ssh route" "ssh" "$OUT"
check "status shows https route" "https" "$OUT"

# 2. X-Protocol: ssh → SSH backend
OUT=$(curl -sf -H "X-Protocol: ssh" "http://127.0.0.1:$PORT/connect/")
check "X-Protocol: ssh routes to SSH backend" "SSH backend" "$OUT"
check "SSH backend response has OpenSSH" "OpenSSH" "$OUT"

# 3. X-Protocol: https → HTTPS backend
OUT=$(curl -sf -H "X-Protocol: https" "http://127.0.0.1:$PORT/connect/")
check "X-Protocol: https routes to HTTPS backend" "HTTPS backend" "$OUT"
check "HTTPS backend response mentions TLS" "TLS" "$OUT"

# 4. X-Protocol: http → HTTP backend
OUT=$(curl -sf -H "X-Protocol: http" "http://127.0.0.1:$PORT/connect/")
check "X-Protocol: http routes to HTTP backend" "HTTP backend" "$OUT"

# 5. Unknown protocol → 400
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -H "X-Protocol: ftp" "http://127.0.0.1:$PORT/connect/")
check_code "Unknown protocol returns 400" "400" "$CODE"

# 6. No protocol header → 400 (treated as unknown)
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    "http://127.0.0.1:$PORT/connect/")
check_code "No X-Protocol header returns 400" "400" "$CODE"

# 7. Stats updated after routing requests
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "stats show ssh count > 0" '"ssh": 1' "$OUT"
check "stats show https count > 0" '"https": 1' "$OUT"
check "stats show http count > 0" '"http": 1' "$OUT"
check "stats show unknown count" '"unknown": 2' "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
