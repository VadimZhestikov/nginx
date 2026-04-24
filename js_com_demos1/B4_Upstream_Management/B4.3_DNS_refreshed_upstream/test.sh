#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8147
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

# Test 1: /dns-status/ returns JSON with hosts
OUT=$(curl -s "http://localhost:$PORT/dns-status/")
check "hosts field present" '"hosts"' "$OUT"
check "api.example.com present" 'api.example.com' "$OUT"
check "db.example.com present" 'db.example.com' "$OUT"
check "resolvedAt timestamp present" '"resolvedAt"' "$OUT"

# Test 2: initial IPs are correct (first cycle)
check "initial api IP" '93.184.216.34' "$OUT"
check "initial db IP" '10.0.1.10' "$OUT"

# Test 3: after refresh the IP set changes
curl -s -X POST "http://localhost:$PORT/admin/refresh/" > /dev/null
OUT2=$(curl -s "http://localhost:$PORT/dns-status/")
check "after refresh api IP changes" '93.184.216.36' "$OUT2"
check "after refresh db IP changes" '10.0.1.11' "$OUT2"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
