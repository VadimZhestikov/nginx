#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8164
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

check_absent() {
    local desc="$1" absent="$2" actual="$3"
    if echo "$actual" | grep -qF "$absent"; then
        echo "FAIL: $desc (found '$absent' but should not)"
        FAIL=$((FAIL+1))
    else
        echo "PASS: $desc"
        PASS=$((PASS+1))
    fi
}

# /passthrough/ — raw body with comments
PT=$(curl -s "http://localhost:${PORT}/passthrough/")
echo "=== Passthrough (raw, with comments) ==="
echo "$PT"
check "passthrough has comment lines" "# This file" "$PT"
check "passthrough has data lines"    "host=db.internal" "$PT"

# /data/ — filtered body (no comment lines)
DATA=$(curl -s "http://localhost:${PORT}/data/")
echo ""
echo "=== Filtered (no comments) ==="
echo "$DATA"
check_absent "filtered output has no '# This file' comment" "# This file" "$DATA"
check_absent "filtered output has no '# DO NOT EDIT' comment" "# DO NOT EDIT" "$DATA"
check_absent "filtered output has no '# Connection pool' comment" "# Connection pool" "$DATA"
check_absent "filtered output has no '# Timeout' comment" "# Timeout" "$DATA"
check "filtered output retains host=db.internal"   "host=db.internal"   "$DATA"
check "filtered output retains port=5432"          "port=5432"          "$DATA"
check "filtered output retains pool_min=2"         "pool_min=2"         "$DATA"
check "filtered output retains connect_timeout"    "connect_timeout=3000" "$DATA"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
