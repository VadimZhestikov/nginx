#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8192
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

# 1. Default User-Agent (no UA or browser-like) → browser path
OUT=$(curl -sf -H "User-Agent: Mozilla/5.0 (X11; Linux x86_64)" \
    "http://127.0.0.1:$PORT/detect/")
check "browser UA routes to browser backend" "Welcome, human" "$OUT"

# 2. Check X-Client-Class header
OUT=$(curl -si -H "User-Agent: Mozilla/5.0" "http://127.0.0.1:$PORT/detect/" | tr -d '\r')
check "browser detection sets X-Client-Class: browser" "X-Client-Class: browser" "$OUT"

# 3. Bot User-Agent → bot path
OUT=$(curl -sf -H "User-Agent: Googlebot/2.1" \
    "http://127.0.0.1:$PORT/detect/")
check "Googlebot UA routes to bot backend" "bot detected" "$OUT"
check "bot response mentions limited content" "limited" "$OUT"

# 4. curl User-Agent → bot path
OUT=$(curl -sf -H "User-Agent: curl/7.88.1" \
    "http://127.0.0.1:$PORT/detect/")
check "curl UA classified as bot" "bot detected" "$OUT"

# 5. Known bot fingerprint → bot path (regardless of UA)
OUT=$(curl -sf \
    -H "User-Agent: Mozilla/5.0 (appears human)" \
    -H "X-TLS-Fingerprint: a0e9f5d64349fb13191bc781f81f42e1" \
    "http://127.0.0.1:$PORT/detect/")
check "known bot fingerprint routes to bot backend" "bot detected" "$OUT"

OUT=$(curl -si \
    -H "User-Agent: Mozilla/5.0 (appears human)" \
    -H "X-TLS-Fingerprint: a0e9f5d64349fb13191bc781f81f42e1" \
    "http://127.0.0.1:$PORT/detect/" | tr -d '\r')
check "fingerprint reason includes known_fingerprint" "known_fingerprint" "$OUT"

# 6. API client User-Agent → api path
OUT=$(curl -sf -H "User-Agent: okhttp/4.10.0" \
    "http://127.0.0.1:$PORT/detect/")
check "okhttp UA routes to API backend" "api" "$OUT"

OUT=$(curl -si -H "User-Agent: okhttp/4.10.0" "http://127.0.0.1:$PORT/detect/" | tr -d '\r')
check "API client sets X-Client-Class: api" "X-Client-Class: api" "$OUT"

# 7. Spider crawler → bot
OUT=$(curl -sf -H "User-Agent: my-spider/1.0 (crawler)" \
    "http://127.0.0.1:$PORT/detect/")
check "spider UA classified as bot" "bot detected" "$OUT"

# 8. Status shows routing stats
OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "status shows routing_stats" "routing_stats" "$OUT"
check "status shows browser count" "browser" "$OUT"
check "status shows bot count" "bot" "$OUT"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
