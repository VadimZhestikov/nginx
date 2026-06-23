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
# addServer is irreversible (cscf is never reclaimed from cycle->pool) — the gate
# must block it. The engine no-ops addServer (no branch), so confirm is harmless.
DANGER='[{"op":"addServer","name":"new.local"}]'
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
check "annotate: addServer is irreversible" '"class":"irreversible"' "$ANN2"

# ── 4. Layer 2 gate: irreversible apply blocked without confirm ──────────────
GC=$(codep "/c/apply?id=0002-danger")
[ "$GC" = "409" ] && { echo "PASS: irreversible apply blocked (HTTP 409)"; PASS=$((PASS+1)); } \
                  || { echo "FAIL: expected 409, got $GC"; FAIL=$((FAIL+1)); }
GBODY=$(post "/c/apply?id=0002-danger")
check "gate names the blocking op" 'addServer' "$GBODY"

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

# ── 9. Reversible removeServer: snapshot hides ghost.local, rollback restores ─
# removeServer is guarded+reversible (tombstone). Host: ghost.local /who answers
# "ghost"; after the snapshot it falls through to the default server (404);
# rollback restores it on every worker.
ghost() { curl -s -H 'Host: ghost.local' "http://127.0.0.1:$PORT/who"; }
ghostc(){ curl -s -o /dev/null -w '%{http_code}' -H 'Host: ghost.local' "http://127.0.0.1:$PORT/who"; }
check "ghost.local serves before hide" 'ghost' "$(ghost)"
post "/c/raw?name=hide-srv&ops=$(Q '[{"op":"removeServer","name":"ghost.local"}]')" > /dev/null
post "/c/apply?id=0004-hide-srv" > /dev/null
sleep 0.3
SHID=1; for i in $(seq 1 12); do [ "$(ghostc)" = "404" ] || SHID=0; done
[ "$SHID" = "1" ] && { echo "PASS: ghost.local removed (404) on all workers"; PASS=$((PASS+1)); } \
                  || { echo "FAIL: ghost.local not removed on all workers"; FAIL=$((FAIL+1)); }
post "/c/rollback" > /dev/null
sleep 0.3
SRES=1; for i in $(seq 1 12); do [ "$(ghost)" = "ghost" ] || SRES=0; done
[ "$SRES" = "1" ] && { echo "PASS: ghost.local restored on all workers after rollback"; PASS=$((PASS+1)); } \
                  || { echo "FAIL: ghost.local not restored on all workers"; FAIL=$((FAIL+1)); }

# ── 10. Reversible removeListener: snapshot pauses :8136, rollback resumes ───
# removeListener is guarded+reversible (soft pause). :8136 serves the console
# server; after the snapshot, accept is paused on every worker (requests time
# out / queue); rollback re-arms accept on all workers.
l8136() { curl -s -o /dev/null -w '%{http_code}' --max-time 2 "http://127.0.0.1:8136/worker"; }
check "second listener :8136 serves before pause" "200" "$(l8136)"
post "/c/raw?name=pause-lsn&ops=$(Q '[{"op":"removeListener","addr":"127.0.0.1:8136"}]')" > /dev/null
post "/c/apply?id=0005-pause-lsn" > /dev/null
sleep 0.3
# paused: curl --max-time returns 000 (no response within timeout) on every worker
LPA=1; for i in $(seq 1 12); do [ "$(l8136)" = "000" ] || LPA=0; done
[ "$LPA" = "1" ] && { echo "PASS: :8136 paused (no response) on all workers"; PASS=$((PASS+1)); } \
                 || { echo "FAIL: :8136 still responding on some worker"; FAIL=$((FAIL+1)); }
# control port unaffected
check ":8135 console unaffected during pause" "200" "$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/worker")"
post "/c/rollback" > /dev/null
sleep 0.3
LRE=1; for i in $(seq 1 12); do [ "$(l8136)" = "200" ] || LRE=0; done
[ "$LRE" = "1" ] && { echo "PASS: :8136 resumed (200) on all workers after rollback"; PASS=$((PASS+1)); } \
                 || { echo "FAIL: :8136 not resumed on all workers"; FAIL=$((FAIL+1)); }

# ── 11. No error-log noise ───────────────────────────────────────────────────
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
