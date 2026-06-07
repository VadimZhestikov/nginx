#!/usr/bin/env bash
# A2.8 — Live Header Mutation test
#
# Verifies:
#   1. Initial X-Api-Header value is "v1-initial" on all workers
#   2. POST /admin/set-header/ changes the value instantly (no reload)
#   3. All 4 workers serve the new value immediately
#   4. X-Api-Header appears in the HTTP response headers of /api/
#   5. WebSocket: poll shows current value; set changes it; poll confirms;
#      subsequent HTTP requests see the WS-set value
set -euo pipefail

DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../../objs/nginx
PORT=8117
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs

cleanup() { "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true; }
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.4

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "PASS: $desc"
        PASS=$((PASS+1))
    else
        echo "FAIL: $desc"
        echo "      expected: $expected"
        echo "      got:      $actual"
        FAIL=$((FAIL+1))
    fi
}

# ── 1. Initial state ──────────────────────────────────────────────────────────

BODY=$(curl -sf "http://127.0.0.1:$PORT/api/")
check "initial /api/ body: v1-initial" '"v1-initial"' "$BODY"

HDRS=$(curl -sI "http://127.0.0.1:$PORT/api/" | tr -d '\r')
check "initial /api/ X-Api-Header response header" "X-Api-Header: v1-initial" "$HDRS"

OUT=$(curl -sf "http://127.0.0.1:$PORT/status/")
check "/status/ shows v1-initial" "v1-initial" "$OUT"

# ── 2. Change via admin REST endpoint ─────────────────────────────────────────

OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/set-header/?value=v2-premium")
check "admin set: ok=true" '"ok":true' "$OUT"
check "admin set: confirms v2-premium" "v2-premium" "$OUT"

# nginx.shared writes are immediately visible — no sleep needed.

# ── 3. All 4 workers serve the new value ──────────────────────────────────────

declare -A seen_pids
for i in $(seq 1 80); do
    OUT=$(curl -sf "http://127.0.0.1:$PORT/api/")
    check "/api/ after admin set, request $i: v2-premium" "v2-premium" "$OUT"
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && seen_pids["$PID"]=1
done

if [ "${#seen_pids[@]}" -ge 4 ]; then
    echo "PASS: all 4 workers serve v2-premium (PIDs: ${!seen_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: only ${#seen_pids[@]} of 4 workers responded (PIDs: ${!seen_pids[*]})"
    FAIL=$((FAIL+1))
fi

# ── 4. Response header is updated on /api/ ────────────────────────────────────

HDRS=$(curl -sI "http://127.0.0.1:$PORT/api/" | tr -d '\r')
check "/api/ X-Api-Header response header updated" "X-Api-Header: v2-premium" "$HDRS"

# ── 5. WebSocket test ─────────────────────────────────────────────────────────
#
# The WS test:
#   a) Resets header to v1-initial
#   b) Opens a WebSocket connection (HTTP 101 upgrade)
#   c) Polls — receives v1-initial
#   d) Sends {"type":"set","value":"v3-ws-live"} — changes header via WS
#   e) Polls again — receives v3-ws-live (same connection, no reconnect)
#   f) Closes the connection
#
# After the WS test the header is v3-ws-live.  Step 6 confirms HTTP clients
# (possibly different workers) also see this value.

echo ""
echo "--- WebSocket test ---"

curl -sf -X POST "http://127.0.0.1:$PORT/admin/set-header/?value=v1-initial" > /dev/null

if command -v python3 >/dev/null 2>&1; then
    # Write the WS test to a temp file — heredoc-inside-$() does not parse in bash.
    TMPPY=$(mktemp /tmp/ws_test_XXXXXX.py)
    cat > "$TMPPY" << 'PYEOF'
import socket, base64, json

HOST, PORT = '127.0.0.1', 8117
s = socket.socket()
s.connect((HOST, PORT))
s.settimeout(5)

key = base64.b64encode(b'a2.8demotest0000').decode()
s.sendall(('GET /ws/ HTTP/1.1\r\n'
           'Host: localhost\r\n'
           'Upgrade: websocket\r\n'
           'Connection: Upgrade\r\n'
           'Sec-WebSocket-Key: ' + key + '\r\n'
           'Sec-WebSocket-Version: 13\r\n\r\n').encode())

resp = b''
while b'\r\n\r\n' not in resp:
    resp += s.recv(4096)
hdr_part, _, leftover = resp.partition(b'\r\n\r\n')
assert b'101' in hdr_part, 'expected HTTP 101, got: ' + repr(hdr_part[:200])
buf = bytearray(leftover)

def need(n):
    global buf
    while len(buf) < n:
        buf.extend(s.recv(4096))

def recv_text():
    global buf
    need(2)
    pay_len = buf[1] & 0x7F   # MASK bit is 0 for server->client frames
    pos = 2
    if pay_len == 126:
        need(4)
        pay_len = (buf[2] << 8) | buf[3]
        pos = 4
    need(pos + pay_len)
    text = bytes(buf[pos:pos + pay_len]).decode('utf-8')
    del buf[:pos + pay_len]
    return text

def send_text(text):
    p     = text.encode('utf-8')
    mask  = b'\x01\x02\x03\x04'
    masked = bytes(p[i] ^ mask[i % 4] for i in range(len(p)))
    n = len(p)
    hbytes = bytes([0x81, 0x80 | n]) if n < 126 else bytes([0x81, 0x80 | 126, n >> 8, n & 0xFF])
    s.sendall(hbytes + mask + masked)

# poll 1: initial state
send_text(json.dumps({'type': 'poll'}))
m = json.loads(recv_text())
assert m.get('type') == 'state', 'expected state, got: ' + str(m)
assert m.get('header') == 'v1-initial', 'poll1: expected v1-initial, got ' + str(m.get('header'))
print('poll1:' + m['header'] + ':worker' + str(m['worker']))

# set via WS — same connection, no reconnect
send_text(json.dumps({'type': 'set', 'value': 'v3-ws-live'}))
m = json.loads(recv_text())
assert m.get('type') == 'state', 'expected state after set, got: ' + str(m)
assert m.get('header') == 'v3-ws-live', 'set: expected v3-ws-live, got ' + str(m.get('header'))
print('set:' + m['header'] + ':ver' + str(m['version']))

# poll 2: confirm value persists on the same connection
send_text(json.dumps({'type': 'poll'}))
m = json.loads(recv_text())
assert m.get('header') == 'v3-ws-live', 'poll2: expected v3-ws-live, got ' + str(m.get('header'))
print('poll2:' + m['header'])

s.close()
PYEOF

    if WS_OUT=$(python3 "$TMPPY" 2>&1); then
        check "WS: poll1 sees v1-initial"     "poll1:v1-initial"  "$WS_OUT"
        check "WS: set returns v3-ws-live"    "set:v3-ws-live"    "$WS_OUT"
        check "WS: poll2 confirms v3-ws-live" "poll2:v3-ws-live"  "$WS_OUT"
    else
        echo "FAIL: WebSocket Python test exited with error:"
        echo "$WS_OUT"
        FAIL=$((FAIL+3))
    fi
    rm -f "$TMPPY"
else
    echo "SKIP: python3 not found — skipping WebSocket test (install python3 to enable)"
fi

# ── 6. HTTP clients see the WS-set value ─────────────────────────────────────

declare -A seen_pids2
for i in $(seq 1 40); do
    OUT=$(curl -sf "http://127.0.0.1:$PORT/api/")
    check "/api/ sees WS-set value (request $i): v3-ws-live" "v3-ws-live" "$OUT"
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && seen_pids2["$PID"]=1
done

if [ "${#seen_pids2[@]}" -ge 4 ]; then
    echo "PASS: all 4 workers serve WS-set value (PIDs: ${!seen_pids2[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: only ${#seen_pids2[@]} of 4 workers in WS-set check"
    FAIL=$((FAIL+1))
fi

# ── 7. Admin set is idempotent ────────────────────────────────────────────────

curl -sf -X POST "http://127.0.0.1:$PORT/admin/set-header/?value=v2-premium" > /dev/null
curl -sf -X POST "http://127.0.0.1:$PORT/admin/set-header/?value=v2-premium" > /dev/null
OUT=$(curl -sf "http://127.0.0.1:$PORT/api/")
check "double set is idempotent: v2-premium" "v2-premium" "$OUT"

# ══════════════════════════════════════════════════════════════════════════════
# Part 2: config-phase add_header mutation (loc.headers.addHeader API)
#
# /plain/ has no JS content handler — nginx responds via `return 200`.
# We inject/remove/clear headers through the admin COM API and verify
# that the actual HTTP response headers change without any reload.
# ══════════════════════════════════════════════════════════════════════════════

echo ""
echo "── Part 2: config-phase add_header on /plain/ (cfgbus cross-worker) ─────────"
#
# Each admin call applies the change on the receiving worker AND posts to cfgbus.js.
# The SW fans the message to all other worker ports; each worker applies it in the
# next event-loop iteration (typically < 1 ms).
#
# settle(): allow one event-loop pass on all workers so SW fan-out completes.
settle() { sleep 0.2; }

# Helper: verify that ALL 4 workers have the expected header in /admin/get-config-headers/
check_all_workers() {
    local desc="$1" expected="$2"
    declare -A wmap
    local ok=1
    for i in $(seq 1 80); do
        local OUT
        OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/get-config-headers/")
        if ! echo "$OUT" | grep -qF "$expected"; then
            ok=0
        fi
        local PID
        PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
        [ -n "$PID" ] && wmap["$PID"]=1
    done
    if [ "$ok" -eq 1 ] && [ "${#wmap[@]}" -ge 4 ]; then
        echo "PASS: all 4 workers: $desc (PIDs: ${!wmap[*]})"
        PASS=$((PASS+1))
    elif [ "${#wmap[@]}" -lt 4 ]; then
        echo "FAIL: only ${#wmap[@]} of 4 workers responded for: $desc"
        FAIL=$((FAIL+1))
    else
        echo "FAIL: some workers missing expected value for: $desc"
        FAIL=$((FAIL+1))
    fi
}

# ── 8. Initial state: /plain/ has no custom X-* headers ──────────────────────

HDRS=$(curl -sI "http://127.0.0.1:$PORT/plain/" | tr -d '\r')
check "initial /plain/: 200 OK" "200 OK" "$HDRS"
if echo "$HDRS" | grep -qiE "^X-"; then
    echo "FAIL: /plain/ already has X-* headers before any injection"
    FAIL=$((FAIL+1))
else
    echo "PASS: /plain/ has no custom X-* headers initially"
    PASS=$((PASS+1))
fi

# ── 9. addHeader propagates to all 4 workers via cfgbus ──────────────────────

OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/add-config-header/?key=X-Feature&value=beta")
check "add-config-header: ok=true"         '"ok":true'         "$OUT"
check "add-config-header: action=addHeader" '"action":"addHeader"' "$OUT"

settle  # allow SW fan-out to reach all workers

# Verify /plain/ HTTP response headers on all workers (reuseport distributes)
declare -A plain_pids
for i in $(seq 1 40); do
    HDRS=$(curl -sI "http://127.0.0.1:$PORT/plain/" | tr -d '\r')
    check "after addHeader: /plain/ has X-Feature: beta (req $i)" "X-Feature: beta" "$HDRS"
done

# Verify all 4 workers' configs via get-config-headers
check_all_workers "X-Feature: beta on all workers" '"key":"X-Feature"'

# ── 10. Second addHeader accumulates alongside first ─────────────────────────

curl -sf -X POST "http://127.0.0.1:$PORT/admin/add-config-header/?key=X-Tag&value=demo" > /dev/null
settle

check_all_workers "both X-Feature and X-Tag on all workers (X-Feature)" '"key":"X-Feature"'
check_all_workers "both X-Feature and X-Tag on all workers (X-Tag)"     '"key":"X-Tag"'

HDRS=$(curl -sI "http://127.0.0.1:$PORT/plain/" | tr -d '\r')
check "both headers in /plain/ response: X-Feature" "X-Feature: beta" "$HDRS"
check "both headers in /plain/ response: X-Tag"     "X-Tag: demo"     "$HDRS"

# ── 11. addHeader with always:true propagates to all workers ─────────────────

curl -sf -X POST "http://127.0.0.1:$PORT/admin/add-config-header/?key=X-Always&value=yes&always=1" > /dev/null
settle

check_all_workers "always=true on all workers" '"always":true'

# ── 12. removeHeader propagates to all workers ────────────────────────────────

OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/remove-config-header/?key=X-Feature")
check "remove-config-header: ok=true" '"ok":true' "$OUT"
settle

# X-Feature must be gone on ALL workers; X-Tag and X-Always must remain
check_all_workers "X-Tag still on all workers after removing X-Feature" '"key":"X-Tag"'
check_all_workers "X-Always still on all workers after removing X-Feature" '"key":"X-Always"'

# Spot-check /plain/ response (no X-Feature)
HDRS=$(curl -sI "http://127.0.0.1:$PORT/plain/" | tr -d '\r')
if echo "$HDRS" | grep -q "^X-Feature:"; then
    echo "FAIL: X-Feature still in /plain/ response after removeHeader"
    FAIL=$((FAIL+1))
else
    echo "PASS: X-Feature absent from /plain/ response after removeHeader"
    PASS=$((PASS+1))
fi
check "/plain/ still has X-Tag after removeHeader" "X-Tag: demo" "$HDRS"

# ── 13. removeHeader is case-insensitive ─────────────────────────────────────

curl -sf -X POST "http://127.0.0.1:$PORT/admin/add-config-header/?key=X-Case&value=test" > /dev/null
settle
curl -sf -X POST "http://127.0.0.1:$PORT/admin/remove-config-header/?key=x-case" > /dev/null
settle

OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/get-config-headers/")
if echo "$OUT" | grep -qi '"key":"X-Case"'; then
    echo "FAIL: X-Case still in get-config-headers after case-insensitive remove"
    FAIL=$((FAIL+1))
else
    echo "PASS: removeHeader is case-insensitive"
    PASS=$((PASS+1))
fi

# ── 14. clearHeaders wipes all entries on all workers ────────────────────────

OUT=$(curl -sf -X POST "http://127.0.0.1:$PORT/admin/clear-config-headers/")
check "clear-config-headers: ok=true" '"ok":true' "$OUT"
settle

# All 4 workers must have empty list
declare -A clear_pids
local_ok=1
for i in $(seq 1 80); do
    OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/get-config-headers/")
    if ! echo "$OUT" | grep -qF '"headers":[]'; then
        local_ok=0
    fi
    PID=$(echo "$OUT" | grep -o '"worker":"[0-9]*"' | grep -o '[0-9]*')
    [ -n "$PID" ] && clear_pids["$PID"]=1
done
if [ "$local_ok" -eq 1 ] && [ "${#clear_pids[@]}" -ge 4 ]; then
    echo "PASS: all 4 workers have empty addHeaders after clearHeaders (PIDs: ${!clear_pids[*]})"
    PASS=$((PASS+1))
else
    echo "FAIL: clearHeaders did not propagate to all 4 workers"
    FAIL=$((FAIL+1))
fi

HDRS=$(curl -sI "http://127.0.0.1:$PORT/plain/" | tr -d '\r')
if echo "$HDRS" | grep -qiE "^X-"; then
    echo "FAIL: /plain/ still has X-* headers after clearHeaders"
    FAIL=$((FAIL+1))
else
    echo "PASS: /plain/ has no X-* headers after clearHeaders"
    PASS=$((PASS+1))
fi

# ── 15. sub-pool reuse — multiple cycles leave no stale data on all workers ───

for i in 1 2 3; do
    curl -sf -X POST "http://127.0.0.1:$PORT/admin/add-config-header/?key=X-Cycle&value=cycle$i" > /dev/null
    curl -sf -X POST "http://127.0.0.1:$PORT/admin/clear-config-headers/" > /dev/null
    settle
done
curl -sf -X POST "http://127.0.0.1:$PORT/admin/add-config-header/?key=X-Final&value=final" > /dev/null
settle

check_all_workers "only X-Final after 3 add/clear cycles (all workers)" '"key":"X-Final"'

OUT=$(curl -sf "http://127.0.0.1:$PORT/admin/get-config-headers/")
if echo "$OUT" | grep -q '"key":"X-Cycle"'; then
    echo "FAIL: stale X-Cycle header in get-config-headers after clear cycles"
    FAIL=$((FAIL+1))
else
    echo "PASS: no stale X-Cycle (sub-pool freed correctly)"
    PASS=$((PASS+1))
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
