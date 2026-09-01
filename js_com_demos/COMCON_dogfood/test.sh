#!/usr/bin/env bash
# COMCON dogfood — increment A acceptance test.
#
# A confined tenant serves real traffic across 2 workers. We assert the policy
# works (mirror tagging + request-header echo), the cage holds on live requests
# (deny-by-default + the A1 reach gate on a granted socket), CRLF injection is
# dropped, and the host's denial report reflects the enforce-mode denials.
set -uo pipefail

DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
PORT=8119
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs
: > logs/error.log

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

check() { # desc, condition-substring, haystack
    if grep -qi -- "$2" <<<"$3"; then
        echo "  PASS: $1"; PASS=$((PASS+1))
    else
        echo "  FAIL: $1"; echo "        wanted [$2] in:"; sed 's/^/          /' <<<"$3"
        FAIL=$((FAIL+1))
    fi
}
absent() { # desc, forbidden-substring, haystack
    if grep -qi -- "$2" <<<"$3"; then
        echo "  FAIL: $1 (found forbidden [$2])"; FAIL=$((FAIL+1))
    else
        echo "  PASS: $1"; PASS=$((PASS+1))
    fi
}

"$NGINX" -p . -c nginx.conf
sleep 0.6

echo "== 1. the caged tenant serves real traffic =="
R=$(curl -s --max-time 3 -D- -A "dogfood/1.0" "http://127.0.0.1:$PORT/mirror")
check "200 from the tenant"                    "HTTP/1.1 200"          "$R"
check "mirror policy tagged the response"      "X-Mirror-Count:"       "$R"
check "request header echoed in (A3.1 data-in)" "X-Mirror-Seen-UA: dogfood/1.0" "$R"
check "body produced by the tenant"            "mirror GET /mirror"    "$R"

echo "== 2. the cage holds on live requests =="
check "deny-by-default + A1 gate: caged=yes"   "X-Mirror-Caged: yes"   "$R"
check "body confirms caged"                    "caged=yes"             "$R"

echo "== 3. CRLF header injection is dropped (A3.1 data-out guard) =="
absent "no smuggled X-Evil header"             "X-Evil:"               "$R"
absent "no smuggled injection header"          "X-Mirror-Try-Inject:"  "$R"

echo "== 4. multi-worker: many requests all stay caged =="
allcaged=1
for i in $(seq 1 30); do
    c=$(curl -s --max-time 3 "http://127.0.0.1:$PORT/mirror" | grep -o 'caged=[a-zA-Z]*')
    [ "$c" = "caged=yes" ] || allcaged=0
done
check "30 requests across workers, all caged"  "1"  "$allcaged"

echo "== 5. host closes the audit->enforce loop =="
D=$(curl -s --max-time 3 "http://127.0.0.1:$PORT/denials")
echo "  denial report: $D"
check "report is enforce mode"                 '"mode":"enforce"'      "$D"
check "sock.listener denials were counted"     '"sock.listener":'      "$D"
# after the traffic above, the per-worker counter this request hit is > 0
absent "counter is not zero-only"              '"total":0'             "$D"

echo
echo "==================================================="
echo "  COMCON dogfood: PASS=$PASS FAIL=$FAIL"
echo "==================================================="
[ "$FAIL" -eq 0 ]
