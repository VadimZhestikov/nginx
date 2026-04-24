#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8182
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

# 1. GET /waf/rules/ — check rule set is loaded
OUT=$(curl -sf "http://127.0.0.1:$PORT/waf/rules/")
check "WAF rules endpoint returns total_rules" "total_rules" "$OUT"
check "WAF rules includes sqli type" "sqli" "$OUT"
check "WAF rules includes xss type" "xss" "$OUT"

# 2. Clean JSON body — passes through
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST -H "Content-Type: application/json" \
    -d '{"name":"Alice","email":"alice@example.com"}' \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "clean JSON body returns 200" "200" "$CODE"

# 3. SQL injection in body — blocked with 400
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST -H "Content-Type: application/json" \
    -d "' OR 1=1 --" \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "SQLi body returns 400" "400" "$CODE"

OUT=$(curl -s -X POST -H "Content-Type: application/json" \
    -d "' OR 1=1 --" \
    "http://127.0.0.1:$PORT/api/submit/")
check "SQLi block message mentions WAF" "WAF" "$OUT"

# 4. UNION-based SQLi — blocked
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST \
    -d "username=admin&password=x' UNION SELECT password FROM users--" \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "UNION SQLi returns 400" "400" "$CODE"

# 5. XSS payload — blocked
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST -H "Content-Type: application/json" \
    -d '<script>alert(1)</script>' \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "XSS script tag returns 400" "400" "$CODE"

OUT=$(curl -s -X POST -H "Content-Type: application/json" \
    -d '<script>alert(1)</script>' \
    "http://127.0.0.1:$PORT/api/submit/")
check "XSS block message identifies type" "xss" "$OUT"

# 6. onerror XSS — blocked
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST \
    -d '<img src=x onerror=alert(1)>' \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "onerror XSS returns 400" "400" "$CODE"

# 7. Another clean submission — passes
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST -H "Content-Type: application/json" \
    -d '{"message":"Hello, World!","count":42}' \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "second clean body returns 200" "200" "$CODE"

# 8. DROP TABLE — blocked
CODE=$(curl -s -o /dev/null -w "%{http_code}" \
    -X POST \
    -d "comment='; DROP TABLE users; --" \
    "http://127.0.0.1:$PORT/api/submit/")
check_code "DROP TABLE SQLi returns 400" "400" "$CODE"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
