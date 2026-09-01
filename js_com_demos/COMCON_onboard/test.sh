#!/usr/bin/env bash
# COMCON onboarding demo — learn mode -> generated grant stub.
set -uo pipefail

DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
PORT=8120
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs; : > logs/error.log

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

check() {  # desc, needle, haystack
    if grep -qF -- "$2" <<<"$3"; then echo "  PASS: $1"; PASS=$((PASS+1))
    else echo "  FAIL: $1"; echo "        wanted [$2] in:"; sed 's/^/          /' <<<"$3"; FAIL=$((FAIL+1)); fi
}

"$NGINX" -p . -c nginx.conf
sleep 0.6

echo "== drive the tenant in learn mode (harvest its reaches) =="
curl -s --max-time 3 "http://127.0.0.1:$PORT/go" >/dev/null

echo "== read the generated onboarding contract =="
C=$(curl -s --max-time 3 "http://127.0.0.1:$PORT/contract")
echo "----- /contract -----"; sed 's/^/  /' <<<"$C"; echo "---------------------"

check "records learn mode"                    "mode observed: learn"          "$C"
check "createSocket classified REFUSE"        "[REFUSE] createSocket()"       "$C"
check "config mutation classified REVIEW"     "[REVIEW] nginx.http.addServer" "$C"
check "network egress classified REVIEW"      "[REVIEW] fetch()"              "$C"
check "emits the enforce next-step"           "js_tenant_mode enforce;"       "$C"
check "summary counts the verdicts"           "to refuse,"                    "$C"

echo
echo "==================================================="
echo "  COMCON onboard: PASS=$PASS FAIL=$FAIL"
echo "==================================================="
[ "$FAIL" -eq 0 ]
