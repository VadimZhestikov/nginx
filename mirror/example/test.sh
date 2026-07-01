#!/usr/bin/env bash
set -euo pipefail
DEMO_DIR="$(cd "$(dirname "$0")" && pwd)"
NGINX=../../objs/nginx
PORT=8300
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs
: > logs/error.log

# self-signed cert for the HTTPS (onClientHello) server
if [ ! -f cert.pem ] || [ ! -f key.pem ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem \
        -days 1 -subj "/CN=mirror-tls" >/dev/null 2>&1
fi

cleanup() {
    "$NGINX" -p . -c nginx.conf -s stop 2>/dev/null || true
    rm -f cert.pem key.pem
}
trap cleanup EXIT

"$NGINX" -p . -c nginx.conf
sleep 0.3

check() {
    local desc="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qiF "$expected"; then
        echo "PASS: $desc"; PASS=$((PASS+1))
    else
        echo "FAIL: $desc (expected '$expected' in: $(echo "$actual" | tr '\n' '|'))"; FAIL=$((FAIL+1))
    fi
}

# --- 1. Spine + REAL per-connection ctx across KEEPALIVE ---------------------
# Two requests over ONE connection (curl reuses it for multiple URLs). The
# per-connection counter must read 1 then 2, and the per-connection SERIAL must
# be identical on both (proving both requests see the SAME per-connection object
# — pilgrim's conn.ctx / r.connCtx, not a per-request object), and the
# accept-time client value must appear (accept->request->response linkage).
OUT=$(curl -s -D - -o /dev/null -H 'X-Mirror-Route: beta' \
        "http://127.0.0.1:$PORT/" "http://127.0.0.1:$PORT/")
REQS=$(echo "$OUT" | grep -i 'x-mirror-conn-reqs:'   | grep -oE '[0-9]+' | tr '\n' ' ')
SER=$(echo "$OUT"  | grep -i 'x-mirror-conn-serial:' | grep -oE '[0-9]+' | tr '\n' ' ')
S1=$(echo "$SER" | awk '{print $1}'); S2=$(echo "$SER" | awk '{print $2}')
check "per-connection counter increments across keepalive (got: $REQS)" "1 2 " "$REQS "
if [ -n "$S1" ] && [ "$S1" = "$S2" ]; then
    echo "PASS: same per-connection object across keepalive (serial $S1 == $S2)"; PASS=$((PASS+1))
else
    echo "FAIL: per-connection serial differs across keepalive ($S1 vs $S2)"; FAIL=$((FAIL+1))
fi
check "accept-time client present on response (spine linkage)" "x-mirror-client: 127.0.0.1" "$OUT"
check "route header reflects request header (beta)" "x-mirror-route: beta" "$OUT"

# --- 2. Distinct connections get distinct serials ----------------------------
A=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/" | grep -i 'x-mirror-conn-serial:' | grep -oE '[0-9]+')
B=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/" | grep -i 'x-mirror-conn-serial:' | grep -oE '[0-9]+')
if [ -n "$A" ] && [ "$A" != "$B" ]; then
    echo "PASS: distinct connections get distinct serials ($A != $B)"; PASS=$((PASS+1))
else
    echo "FAIL: distinct connections did not get distinct serials ($A, $B)"; FAIL=$((FAIL+1))
fi

# --- 3. Per-request routing (no header -> stable) ----------------------------
OUT=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/")
check "no route header -> stable" "x-mirror-route: stable" "$OUT"

# --- 4. Per-event capability gate fires --------------------------------------
OUT=$(curl -s -D - -o /dev/null -H 'X-Mirror-Captest: 1' "http://127.0.0.1:$PORT/")
check "capability gate rejects setResponseHeader in onRequestHeaders" \
      "not valid in event 'onRequestHeaders'" "$OUT"

# --- 5. onClientClose fires (pilgrim conn.onClose), flow-local survives ------
# Several Connection: close requests each open+close a connection -> the close
# hook fires per connection and logs the serial/reqs/client stashed at accept.
for i in 1 2 3; do curl -s -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/"; done
sleep 0.6
CLOSES=$(grep -c 'mirror: onClientClose' logs/error.log || true)
if [ "${CLOSES:-0}" -ge 3 ]; then
    echo "PASS: onClientClose fired on connection close (${CLOSES} events logged)"; PASS=$((PASS+1))
else
    echo "FAIL: onClientClose did not fire enough (${CLOSES} events)"; FAIL=$((FAIL+1))
fi
check "close hook sees accept-time flow-local (client)" "client=127.0.0.1" "$(cat logs/error.log)"

# --- 6. onClientHello (TLS ClientHello inspection; JA3 inputs) ---------------
# The SNI carried in the ClientHello (before the handshake completes) is read
# back on the HTTP response — proving the spine now reaches down to TLS. The
# cipher/extension lists are parsed (the JA3 inputs).
OUT=$(curl -sk -D - -o /dev/null --resolve mirrorsni.test:8343:127.0.0.1 \
        https://mirrorsni.test:8343/)
check "onClientHello: SNI from ClientHello reaches the HTTP response" \
      "x-mirror-sni: mirrorsni.test" "$OUT"
CC=$(echo "$OUT" | grep -i 'x-mirror-cipher-count:' | grep -oE '[0-9]+')
if [ "${CC:-0}" -gt 0 ]; then echo "PASS: onClientHello: cipher suites parsed ($CC)"; PASS=$((PASS+1));
else echo "FAIL: onClientHello: no cipher suites parsed"; FAIL=$((FAIL+1)); fi
VER=$(echo "$OUT" | grep -i 'x-mirror-tls-version:' | grep -oE '[0-9]+')
if [ "${VER:-0}" -gt 0 ]; then echo "PASS: onClientHello: TLS version parsed ($VER)"; PASS=$((PASS+1));
else echo "FAIL: onClientHello: no TLS version"; FAIL=$((FAIL+1)); fi
check "onClientHello: JA3 fingerprint string built" "x-mirror-ja3:" "$OUT"

# --- 7. per-request LB selection (ev.selectUpstream / iRules `pool`) ---------
# The rule picks the pool from the X-Pool header; nginx proxies to the chosen
# upstream. Backends return distinguishable bodies.
check "LB: default -> poolA"     "backend-A" "$(curl -s http://127.0.0.1:$PORT/lb/)"
check "LB: X-Pool: b -> poolB"   "backend-B" "$(curl -s -H 'X-Pool: b' http://127.0.0.1:$PORT/lb/)"
check "LB: X-Pool: a -> poolA"   "backend-A" "$(curl -s -H 'X-Pool: a' http://127.0.0.1:$PORT/lb/)"

# --- 8. peer-level LB (upstream.onSelectPeer / iRules LB::select) ------------
# The custom balancer picks the peer index the request stashed in flow-local.
P0=$(curl -s -H 'X-Peer: 0' http://127.0.0.1:$PORT/lbpeer/)
P1=$(curl -s -H 'X-Peer: 1' http://127.0.0.1:$PORT/lbpeer/)
check "peer 0 reaches a backend" "backend-" "$P0"
check "peer 1 reaches a backend" "backend-" "$P1"
if [ -n "$P0" ] && [ "$P0" != "$P1" ]; then
    echo "PASS: peer index selects distinct nodes ($(echo $P0) vs $(echo $P1))"; PASS=$((PASS+1))
else
    echo "FAIL: peer index did not distinguish nodes ($P0 / $P1)"; FAIL=$((FAIL+1))
fi
check "peer 0 selection is stable" "$P0" "$(curl -s -H 'X-Peer: 0' http://127.0.0.1:$PORT/lbpeer/)"

# --- 9. L4 data events (onClientData / iRules CLIENT_DATA) -------------------
# Send raw TCP bytes to the stream server; the rule inspects the preread bytes
# and detects the protocol. Verified via the log.
python3 - <<'PY' 2>/dev/null
import socket, time
for payload in (b'SSH-2.0-OpenSSH_8.9', b'GET / HTTP/1.0\r\n\r\n'):
    s = socket.create_connection(('127.0.0.1', 8402)); time.sleep(0.05)
    s.sendall(payload); time.sleep(0.3); s.close()
PY
sleep 0.5
check "L4 onClientData detects ssh from raw bytes"  "mirror onClientData: proto=ssh"  "$(cat logs/error.log)"
check "L4 onClientData detects http from raw bytes" "mirror onClientData: proto=http" "$(cat logs/error.log)"

# --- 10. cross-worker shared table (phase 7) --------------------------------
# The mirror `table` is backed by nginx.shared (cross-worker shmem). Fire many
# close-connections; reuseport spreads them across both workers. Under one
# SHARED counter every response carries a DISTINCT running total; a per-worker
# Map would repeat totals between the two workers. We also require >=2 distinct
# worker indices (nginx.workerIdx), so "all distinct" cannot pass trivially by
# every request happening to land on a single worker.
N=40
: > logs/p7_totals; : > logs/p7_widx
BACKEND=""
for i in $(seq 1 $N); do
    H=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/")
    echo "$H" | grep -i '^x-mirror-total:' | grep -oE '[0-9]+' >> logs/p7_totals
    echo "$H" | grep -i '^x-mirror-widx:'  | grep -oE '[0-9]+' >> logs/p7_widx
    if [ -z "$BACKEND" ]; then
        BACKEND=$(echo "$H" | grep -i '^x-mirror-table-backend:' | awk '{print $2}' | tr -d '\r')
    fi
done
check "table backend is cross-worker (nginx.shared)" "shared" "$BACKEND"
DWIDX=$(sort -u logs/p7_widx | grep -c .)
if [ "${DWIDX:-0}" -ge 2 ]; then
    echo "PASS: requests served by multiple workers ($DWIDX distinct worker indices)"; PASS=$((PASS+1))
else
    echo "FAIL: only ${DWIDX:-0} worker index seen — cannot prove cross-worker"; FAIL=$((FAIL+1))
fi
NTOT=$(grep -c . logs/p7_totals)
UTOT=$(sort -u logs/p7_totals | grep -c .)
if [ "$NTOT" -eq "$UTOT" ] && [ "$NTOT" -eq "$N" ]; then
    echo "PASS: all $N table totals distinct -> single shared counter across workers"; PASS=$((PASS+1))
else
    echo "FAIL: table totals not all distinct ($UTOT unique of $NTOT) -> per-worker counters"; FAIL=$((FAIL+1))
fi

# --- 11. table TTL / expiry (phase 8, iRules `table set key val <timeout>`) --
# Set a key with a 3s TTL on one request; it must be readable (cross-worker)
# right after, carry a positive remaining TTL, then be gone after it expires.
SET=$(curl -s -D - -o /dev/null -H 'Connection: close' -H 'X-Mirror-TtlSet: 1' \
        "http://127.0.0.1:$PORT/")
check "TTL: key readable immediately after set" "x-mirror-ttltest: ephemeral" "$SET"
TREM=$(echo "$SET" | grep -i '^x-mirror-ttltest-ttl:' | grep -oE '[0-9]+')
if [ -n "$TREM" ] && [ "$TREM" -ge 1 ] && [ "$TREM" -le 3 ]; then
    echo "PASS: TTL: remaining lifetime reported ($TREM s)"; PASS=$((PASS+1))
else
    echo "FAIL: TTL: bad remaining lifetime ('$TREM')"; FAIL=$((FAIL+1))
fi
# a second request (no set) still sees it — proves it persists cross-worker
STILL=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/")
check "TTL: key still present before expiry (cross-worker)" \
      "x-mirror-ttltest: ephemeral" "$STILL"
# wait past the TTL, then it must be reclaimed
sleep 4
GONE=$(curl -s -D - -o /dev/null -H 'Connection: close' "http://127.0.0.1:$PORT/")
check "TTL: key value gone after expiry"     "x-mirror-ttltest: undefined" "$GONE"
check "TTL: remaining lifetime null after expiry" "x-mirror-ttltest-ttl: null" "$GONE"

# --- 12. live-transpiled iRule (phase 11) ------------------------------------
# /irule/ is served by a genuine iRules TCL rule transpiled to mirror at config
# time. Prove the transpiled control flow + response headers run for real.
ADM=$(curl -s -D - -o /dev/null -H 'X-Irule: admin' -H 'X-Agent: CurL/8' \
        -H 'Cookie: sid=xyz789; other=1' "http://127.0.0.1:$PORT/irule/")
USR=$(curl -s -D - -o /dev/null "http://127.0.0.1:$PORT/irule/")
check "transpiled iRule: starts_with -> admin tier" "x-irule-tier: admin" "$ADM"
check "transpiled iRule: else-branch -> user tier"  "x-irule-tier: user"  "$USR"
# phase-14 command surface, end to end:
check "transpiled iRule: string tolower [HTTP::header]" "x-irule-ua: curl/8" "$ADM"
check "transpiled iRule: HTTP::cookie read"             "x-irule-sid: xyz789" "$ADM"
# phase-15 data groups (class match / class lookup), end to end:
BOT=$(curl -s -D - -o /dev/null -H 'X-Agent: BadBot/9' "http://127.0.0.1:$PORT/irule/")
check "transpiled iRule: class match blocklist -> blk 1" "x-irule-blk: 1" "$BOT"
check "transpiled iRule: class match clean -> blk 0"     "x-irule-blk: 0" "$USR"
check "transpiled iRule: class lookup tier -> team"      "x-irule-team: A-team" "$ADM"
HITS=$(echo "$USR" | grep -i '^x-irule-hits:' | grep -oE '[0-9]+')
if [ -n "$HITS" ] && [ "$HITS" -ge 1 ]; then
    echo "PASS: transpiled iRule: table incr reflected in header ($HITS)"; PASS=$((PASS+1))
else
    echo "FAIL: transpiled iRule: no table hits header ('$HITS')"; FAIL=$((FAIL+1))
fi

# --- 13. session persistence / stickiness (phase 12, iRules `persist`) -------
# /persist/ pins a client (keyed by X-Client) to a peer via the cross-worker
# table. The upstream has TWO distinct backends, so round-robin would split
# requests ~50/50; a sticky client must instead hit ONE backend every time —
# and it must do so across BOTH workers (reuseport spreads the connections),
# which a per-worker map could not guarantee. That single invariant proves
# stickiness AND that it is cross-worker.
sticky_backend() {   # $1 = client key ; prints the single backend, or "SPLIT"
    local client="$1" seen="" b
    for i in $(seq 1 10); do
        b=$(curl -s -H "X-Client: $client" -H 'Connection: close' \
                 "http://127.0.0.1:$PORT/persist/")
        if [ -z "$seen" ]; then seen="$b"; elif [ "$b" != "$seen" ]; then seen="SPLIT"; fi
    done
    echo "$seen" | tr -d '\n'
}
SA=$(sticky_backend alice)
SB=$(sticky_backend bob)
if echo "$SA" | grep -q 'backend-'; then
    echo "PASS: persist: 'alice' pinned to one backend across 10 reqs/2 workers ($SA)"; PASS=$((PASS+1))
else
    echo "FAIL: persist: 'alice' not sticky ($SA)"; FAIL=$((FAIL+1))
fi
if echo "$SB" | grep -q 'backend-'; then
    echo "PASS: persist: 'bob' pinned to one backend across 10 reqs/2 workers ($SB)"; PASS=$((PASS+1))
else
    echo "FAIL: persist: 'bob' not sticky ($SB)"; FAIL=$((FAIL+1))
fi

# --- 14. cookie-insert persistence (phase 13, iRules persist cookie insert) --
# The LB inserts a Set-Cookie encoding the chosen peer on the first response;
# subsequent requests carrying the cookie pin to that peer (stateless — no table
# lookup). Two backends, so without the cookie it would round-robin.
JAR=logs/cookiejar; rm -f "$JAR"
H1=$(curl -s -c "$JAR" -D - -o logs/cbody "http://127.0.0.1:$PORT/cookie/")
B1=$(cat logs/cbody)
check "cookie-insert: first response inserts the pin cookie" "set-cookie: MIRRORPIN=" "$H1"
STICK="$B1"; REISSUE=0
for i in $(seq 1 8); do
    R=$(curl -s -b "$JAR" -c "$JAR" -D - -o logs/cbody "http://127.0.0.1:$PORT/cookie/")
    bb=$(cat logs/cbody)
    if [ "$bb" != "$STICK" ]; then STICK="MISMATCH"; fi
    if echo "$R" | grep -qi '^set-cookie:'; then REISSUE=$((REISSUE+1)); fi
done
if echo "$STICK" | grep -q 'backend-'; then
    echo "PASS: cookie-insert: pinned to $B1 across 8 reqs/2 workers"; PASS=$((PASS+1))
else
    echo "FAIL: cookie-insert: not sticky (drifted to $STICK)"; FAIL=$((FAIL+1))
fi
if [ "$REISSUE" -eq 0 ]; then
    echo "PASS: cookie-insert: cookie not re-issued once pinned"; PASS=$((PASS+1))
else
    echo "FAIL: cookie-insert: cookie re-issued $REISSUE times after pin"; FAIL=$((FAIL+1))
fi

# --- 15. realistic showcase iRule, transpiled + live (phase 16) --------------
# transpile/showcase.tcl applied live on mirror-showcase (:8301). Exercises the
# whole surface end-to-end: path routing (data group), security headers, release
# channel switch, bot flag, and an IP-blocklist 403 from an access-phase rule.
SPORT=8301
API=$(curl -s -D - -o /dev/null "http://127.0.0.1:$SPORT/api/users")
check "showcase: /api/ routed to poolB (backend-B)" "backend-b" \
      "$(curl -s http://127.0.0.1:$SPORT/api/users)"
check "showcase: X-Area header = api"        "x-area: api"          "$API"
check "showcase: security header X-Frame"    "x-frame-options: DENY" "$API"
check "showcase: HSTS header inserted"       "strict-transport-security: max-age=31536000" "$API"
WEB=$(curl -s -D - -o /dev/null -H 'X-Channel: beta' "http://127.0.0.1:$SPORT/home")
check "showcase: default path -> web area"   "x-area: web"          "$WEB"
check "showcase: switch X-Channel -> beta"   "x-channel: beta"      "$WEB"
BOT=$(curl -s -D - -o /dev/null -H 'User-Agent: Nasty-BadBot/3' "http://127.0.0.1:$SPORT/home")
check "showcase: bot UA -> X-Flagged 1"      "x-flagged: 1"         "$BOT"
BLK=$(curl -s -o /dev/null -w '%{http_code}' -H 'X-Forwarded-For: 10.0.0.5' \
        "http://127.0.0.1:$SPORT/home")
if [ "$BLK" = "403" ]; then
    echo "showcase: blocklisted XFF -> 403 (respond from access-phase rule): $BLK"
    echo "PASS: showcase: IP blocklist returns 403"; PASS=$((PASS+1))
else
    echo "FAIL: showcase: blocklist did not 403 (got $BLK) — respond-in-access-hook gap"; FAIL=$((FAIL+1))
fi

# --- 16. external KV tier (phase 17, mirror.kv over r.fetch) ------------------
# An async content handler on /kv/ reads/writes an external HTTP KV service
# (mirror-kvsvc, backed by nginx.shared) via mirror.kv. Prove set+readback, and
# that the value PERSISTS to a later, independent request (separate fetch).
SET=$(curl -s "http://127.0.0.1:$PORT/kv/?k=greeting&set=hello-kv")
check "external KV: set + readback in one request" "hello-kv" "$SET"
GET=$(curl -s "http://127.0.0.1:$PORT/kv/?k=greeting")
check "external KV: value persisted to a later request" "hello-kv" "$GET"
MISS=$(curl -s "http://127.0.0.1:$PORT/kv/?k=nonesuch")
check "external KV: absent key -> MISS" "MISS" "$MISS"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
