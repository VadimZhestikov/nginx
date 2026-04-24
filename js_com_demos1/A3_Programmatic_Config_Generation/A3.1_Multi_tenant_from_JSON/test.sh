#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
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

# Tenant: acme (port 8115)
OUT=$(curl -sf http://127.0.0.1:8115/)
check "acme greeting on port 8115" "Welcome to ACME Corp" "$OUT"

OUT=$(curl -sf http://127.0.0.1:8115/health/)
check "acme health check" "acme-ok" "$OUT"

# Tenant: beta (port 8116)
OUT=$(curl -sf http://127.0.0.1:8116/)
check "beta greeting on port 8116" "Welcome to Beta Inc" "$OUT"

OUT=$(curl -sf http://127.0.0.1:8116/health/)
check "beta health check" "beta-ok" "$OUT"

# Tenant: gamma (port 8117)
OUT=$(curl -sf http://127.0.0.1:8117/)
check "gamma greeting on port 8117" "Welcome to Gamma Labs" "$OUT"

OUT=$(curl -sf http://127.0.0.1:8117/health/)
check "gamma health check" "gamma-ok" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
