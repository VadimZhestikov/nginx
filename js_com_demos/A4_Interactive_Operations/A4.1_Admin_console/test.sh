#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8135
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs snapshots
rm -f snapshots/*.json

"$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
sleep 0.2
cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT
: > logs/error.log     # fresh log AFTER the stop, so its pid-miss noise is dropped
"$NGINX" -p . -c nginx.conf
sleep 0.4

check() {
    local desc="$1" expected="$2" actual="$3"
    if grep -qF -- "$expected" <<<"$actual"; then
        echo "PASS: $desc"; PASS=$((PASS+1))
    else
        echo "FAIL: $desc"; echo "      expected: $expected"
        echo "      got:      ${actual:0:300}"; FAIL=$((FAIL+1))
    fi
}
B="http://127.0.0.1:$PORT"
Q() { python3 -c "import urllib.parse,sys;print(urllib.parse.quote(sys.argv[1]))" "$1"; }
post() { curl -s -X POST "$B$1"; }
codep() { curl -s -o /dev/null -w "%{http_code}" -X POST "$B$1"; }
code()  { curl -s -o /dev/null -w "%{http_code}" "$B$1"; }

# ── 1. Console SPA is served ─────────────────────────────────────────────────
HTML=$(curl -s "$B/")
check "console HTML served"        'nginx COM — Admin Console' "$HTML"
check "console has convergence UI" 'Worker convergence'        "$HTML"

# ── 2. Seed snapshots with varied-class ops ──────────────────────────────────
MIXED='[{"shared":"routes.products","value":"1"},
        {"prop":{"upstream":"demo_backend","peer":"127.0.0.1:9991","property":"weight"},"value":9},
        {"prop":{"server":"localhost","location":"/api/","subobject":"headers","property":"addHeaders"},
         "value":[{"key":"X-Console","value":"on","always":false}]},
        {"op":"addLocation","serverName":"localhost","pattern":"/dynamic/","handler":"dynamicHandler"}]'
post "/c/raw?name=mixed&ops=$(Q "$MIXED")" > /dev/null
# removeServer is still irreversible (Track S pending) — the gate must block it.
# The engine no-ops removeServer (no branch), so confirm-applying it is harmless.
DANGER='[{"op":"removeServer","serverName":"ghost.local"}]'
post "/c/raw?name=danger&ops=$(Q "$DANGER")" > /dev/null

LIST=$(curl -s "$B/c/snapshots")
check "snapshots listed" '0001-mixed' "$LIST"
check "snapshots listed" '0002-danger' "$LIST"

# ── 3. Per-op safety annotation (Layer 1 in the console) ─────────────────────
ANN=$(curl -s "$B/c/snapshot?id=0001-mixed")
check "annotate: shared op is auto-shared"  '"propagation":"auto-shared"' "$ANN"
check "annotate: peer weight is zoned-shared" '"propagation":"zoned-shared"' "$ANN"
check "annotate: addHeaders is safe"        '"label":"localhost/api/.headers.addHeaders' "$ANN"
check "annotate: addLocation is guarded"    '"class":"guarded"' "$ANN"

ANN2=$(curl -s "$B/c/snapshot?id=0002-danger")
check "annotate: removeServer is irreversible" '"class":"irreversible"' "$ANN2"

# ── 4. Layer 2 gate: irreversible apply blocked without confirm ──────────────
GC=$(codep "/c/apply?id=0002-danger")
[ "$GC" = "409" ] && { echo "PASS: irreversible apply blocked (HTTP 409)"; PASS=$((PASS+1)); } \
                  || { echo "FAIL: expected 409, got $GC"; FAIL=$((FAIL+1)); }
GBODY=$(post "/c/apply?id=0002-danger")
check "gate names the blocking op" 'removeServer' "$GBODY"

# ── 5. Apply a safe snapshot → fans out → all workers converge ───────────────
AP=$(post "/c/apply?id=0001-mixed")
check "apply mixed succeeds" '"applied":"0001-mixed"' "$AP"

# poll convergence briefly (fan-out is async)
ALL=0
for i in $(seq 1 20); do
    CV=$(curl -s "$B/c/converge")
    if grep -qF -- '"allConverged":true' <<<"$CV" \
       && grep -qF -- '"desired":"0001-mixed"' <<<"$CV"; then ALL=1; break; fi
    sleep 0.1
done
[ "$ALL" = "1" ] && { echo "PASS: all workers converged to 0001-mixed"; PASS=$((PASS+1)); } \
                 || { echo "FAIL: workers did not all converge"; echo "  $CV"; FAIL=$((FAIL+1)); }

NW=$(curl -s "$B/c/converge" | grep -o '"converged":true' | wc -l)
[ "$NW" -ge 2 ] && { echo "PASS: convergence observed on $NW workers"; PASS=$((PASS+1)); } \
                || { echo "FAIL: convergence on $NW workers (<2)"; FAIL=$((FAIL+1)); }

# the safe ops actually took effect: X-Console header on /api/ across workers
HDR_OK=1
for i in $(seq 1 20); do
    curl -si "$B/api/" | grep -qiF 'X-Console: on' || HDR_OK=0
done
[ "$HDR_OK" = "1" ] && { echo "PASS: X-Console header fanned out to all /api/ responses"; PASS=$((PASS+1)); } \
                    || { echo "FAIL: X-Console header missing on some worker"; FAIL=$((FAIL+1)); }

# ── 6. Rollback to base → desired changes ────────────────────────────────────
post "/c/rollback" > /dev/null
sleep 0.3
RB=$(curl -s "$B/c/converge")
check "rollback updates desired" '"desired":"base"' "$RB"

# ── 7. Gate override: confirm=1 applies the irreversible snapshot ────────────
OV=$(post "/c/apply?id=0002-danger&confirm=1")
check "confirm=1 overrides the gate" '"applied":"0002-danger"' "$OV"

# ── 8. Reversible removeLocation: snapshot hides /api/, rollback restores it ──
# removeLocation is now guarded+reversible (tombstone). Applying hides the route
# on every worker; rollback un-tombstones it (the COW-symmetric round trip).
post "/c/raw?name=hide-api&ops=$(Q '[{"op":"removeLocation","serverName":"localhost","pattern":"/api/"}]')" > /dev/null
echo "  /api/ before hide: $(code /api/)"
post "/c/apply?id=0003-hide-api" > /dev/null
sleep 0.3
HID=1; for i in $(seq 1 12); do [ "$(code /api/)" = "404" ] || HID=0; done
[ "$HID" = "1" ] && { echo "PASS: /api/ hidden (404) on all workers after apply"; PASS=$((PASS+1)); } \
                 || { echo "FAIL: /api/ not hidden on all workers"; FAIL=$((FAIL+1)); }
post "/c/rollback" > /dev/null
sleep 0.3
RES=1; for i in $(seq 1 12); do [ "$(code /api/)" = "200" ] || RES=0; done
[ "$RES" = "1" ] && { echo "PASS: /api/ restored (200) on all workers after rollback"; PASS=$((PASS+1)); } \
                 || { echo "FAIL: /api/ not restored on all workers"; FAIL=$((FAIL+1)); }

# ── 9. No error-log noise ────────────────────────────────────────────────────
# Benign "No such file" lines are expected while /api/ is hidden (the request
# falls through to the static handler) — they are not failures.
if grep -iE '\[error|\[emerg|panic' logs/error.log | grep -vq 'No such file or directory'; then
    echo "FAIL: error log has errors"
    grep -iE '\[error|\[emerg' logs/error.log | grep -v 'No such file' | tail -3
    FAIL=$((FAIL+1))
else
    echo "PASS: error log clean (ignoring expected route-hidden 404s)"; PASS=$((PASS+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
