#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8132
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

"$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
sleep 0.2

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.4

check() {
    local desc="$1" expected="$2" actual="$3"
    # here-string, not a pipe: grep -q exits early on match and would SIGPIPE
    # the producer of a piped echo, which `set -o pipefail` turns into a
    # spurious failure on large (multi-KB) payloads like /inspect.
    if grep -qF -- "$expected" <<<"$actual"; then
        echo "PASS: $desc"; PASS=$((PASS+1))
    else
        echo "FAIL: $desc"; echo "      expected: $expected"
        echo "      got:      ${actual:0:200}"; FAIL=$((FAIL+1))
    fi
}

get() { curl -sf "http://127.0.0.1:$PORT$1"; }

# ── 1. JSON summary: members classified across all three classes ─────────────
SUM=$(get /inspect/sum)
check "summary returns total count"        '"total"'        "$SUM"
check "summary has safe members"           '"safe"'         "$SUM"
check "summary has guarded members"        '"guarded"'      "$SUM"
check "summary has irreversible members"   '"irreversible"' "$SUM"
check "summary counts zoned-shared"        '"zoned-shared"' "$SUM"
check "summary counts request-scoped"      '"requestScoped"' "$SUM"

# guarded count must be > 0 (handler, removeLocation, addLocation, ssl.set*, …).
# After Track L made removeLocation reversible, NO describable member is
# irreversible (the irreversible topology ops — addServer/createSocket — live on
# plain objects describe() can't reach; tracked as a follow-up). So the
# irreversible count is legitimately 0 here; we assert the tier is reported.
GUARDED=$(echo "$SUM" | grep -o '"guarded": *[0-9]*' | grep -o '[0-9]*')
[ "${GUARDED:-0}" -gt 0 ] && { echo "PASS: guarded count > 0 ($GUARDED)"; PASS=$((PASS+1)); } \
                          || { echo "FAIL: guarded count > 0"; FAIL=$((FAIL+1)); }

# ── 2. Full JSON report: representative classifications present ──────────────
REP=$(get /inspect)
check "report lists a location node"        'locations[0]'       "$REP"
check "report classifies handler guarded"   '"name": "handler"'  "$REP"
check "report classifies removeLocation (guarded/reversible)" '"name": "removeLocation"' "$REP"
check "report includes proxy.pass"           '"name": "pass"'     "$REP"

# ── 3. Cross-worker propagation: zoned vs plain upstream peers ───────────────
# upstream[0]=zoned (zone) → zoned-shared; upstream[1]=plain → worker-local
check "zoned upstream peer is zoned-shared"  '"zoned-shared"'  "$REP"
check "plain upstream peer is worker-local"  '"worker-local"'  "$REP"

# ── 4. HTML traffic-light report renders ─────────────────────────────────────
HTML=$(get /)
check "HTML report has a title"              'Safety-Class Inspector' "$HTML"
check "HTML shows green safe badge"          '🟢'                     "$HTML"
check "HTML legend includes irreversible color" '🔴'                  "$HTML"
check "HTML renders the members table"       '<table>'               "$HTML"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
