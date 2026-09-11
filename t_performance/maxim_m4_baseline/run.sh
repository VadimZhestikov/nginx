#!/usr/bin/env bash
# maxim_m4_baseline — re-measure the M1 gate against the C5 baseline.
#
# WHY THIS EXISTS
#
# M1 (t_performance/maxim_m1/RESULTS.md) passed the gate for the typed-policy
# compiler with "hand-written C is 3.44x the interpreted mirror". That number
# is STALE, and the staleness matters: it compared against the INTERPRETER,
# and C5 shipped since, so tenant JS already runs JIT-compiled. The question
# that actually decides M4/M5 is therefore not
#
#     how much faster than the interpreter is compiled C?
#
# but
#
#     how much headroom is left ABOVE THE JIT?
#
# If the JIT already sits near the hand-C ceiling, a typed compiler reclaims
# little and M4/M5 should not be built. That is a multi-week decision resting
# on a number nobody has measured.
#
# ARMS  (all serve the identical "count + tag" policy from M1)
#
#   floor       no JS at all: location / { return 200 "ok\n"; }
#               The machine's ceiling. M1 measured hand-C at 90-96% of this,
#               so it doubles as a proxy for the compiler ceiling.
#   directives  the same work in pure nginx directives (map + add_header).
#               M1's control: it showed the whole gap was interpretation.
#   jit         mirror.js + maxim_mirror_app.js on the JIT build.
#               THE NEW NUMBER. This is the C5 baseline.
#   interp      the same JS on a genuinely non-JIT engine. OPTIONAL and off by
#               default because it needs the libquickjs toggle (see --interp).
#   handc       M1's hand-written C module. OPTIONAL, needs its own build.
#
# TWO GUARDS, both learned the hard way in this project:
#
#   1. It VERIFIES which engine each binary actually contains, rather than
#      trusting the builddir name. objs/nginx links the JIT whenever
#      libquickjs.a was built CONFIG_JIT=y -- so a binary labelled
#      "interpreter" may be nothing of the sort, and an arm labelled "interp"
#      would then silently measure the JIT and answer the question backwards.
#   2. It REFUSES to report numbers on a busy machine unless forced. Perf
#      figures taken while something else saturates the box are worse than no
#      figures, because they get quoted later.
#
# Usage:
#   bash run.sh                     # floor + directives + jit
#   bash run.sh --handc=PATH        # add the hand-C arm (see maxim_m1/config)
#   bash run.sh --interp=PATH       # add a genuine interpreter arm
#   bash run.sh --dry-run           # build configs, start/stop each arm, no load
#   bash run.sh --force-busy        # measure anyway on a loaded box (don't)
#
#   --duration=N   seconds per run   (default 15)
#   --runs=N       runs per arm      (default 3, best-of)
#   --warmup=N     warmup seconds    (default 5)
#   --conns=N      wrk connections   (default 100)
#   --threads=N    wrk threads       (default 4)

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="$HERE/.work"

JIT_BIN="$ROOT/objs_jit/nginx"
HANDC_BIN=""
INTERP_BIN=""
DURATION=15
RUNS=3
WARMUP=5
CONNS=100
THREADS=4
DRY_RUN=0
FORCE_BUSY=0
PORT=8399

for arg in "$@"; do
    case "$arg" in
        --handc=*)   HANDC_BIN="${arg#*=}" ;;
        --interp=*)  INTERP_BIN="${arg#*=}" ;;
        --jit=*)     JIT_BIN="${arg#*=}" ;;
        --duration=*) DURATION="${arg#*=}" ;;
        --runs=*)    RUNS="${arg#*=}" ;;
        --warmup=*)  WARMUP="${arg#*=}" ;;
        --conns=*)   CONNS="${arg#*=}" ;;
        --threads=*) THREADS="${arg#*=}" ;;
        --dry-run)   DRY_RUN=1 ;;
        --force-busy) FORCE_BUSY=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

# ── wrk ──────────────────────────────────────────────────────────────────────
WRK="${WRK:-wrk}"
if ! command -v "$WRK" &>/dev/null; then
    if [ -x /tmp/wrk-src/wrk ]; then WRK=/tmp/wrk-src/wrk
    else echo "wrk not found; build it (see t_performance/run.sh)" >&2; exit 2; fi
fi

# ── Guard 2: is the machine quiet enough to believe? ─────────────────────────
# Load average scaled by CPU count is the WRONG instrument here, and this
# harness got it wrong first time round: one saturated core on a 16-CPU box is
# ~6% by that measure and sails past a 25% threshold, yet it is more than
# enough to move throughput and poison the cache. What matters for a benchmark
# is that the box is IDLE, in absolute terms -- so threshold on absolute load,
# and separately catch any single process burning a core.
CPUS="$(nproc)"
LOAD1="$(awk '{print $1}' /proc/loadavg)"
MAX_LOAD="${MAX_LOAD:-0.5}"

HOG="$(ps -eo pcpu,pid,comm --sort=-pcpu --no-headers 2>/dev/null | head -1)"
HOG_CPU="$(printf '%s' "$HOG" | awk '{print int($1)}')"

BUSY=$(awk -v l="$LOAD1" -v m="$MAX_LOAD" -v h="${HOG_CPU:-0}" \
        'BEGIN{print (l > m || h > 50) ? 1 : 0}')

if [ "$BUSY" = "1" ] && [ "$DRY_RUN" = "0" ] && [ "$FORCE_BUSY" = "0" ]; then
    cat >&2 <<MSG
REFUSING TO MEASURE: the box is not idle.

  load average (1m) : $LOAD1   (threshold $MAX_LOAD; $CPUS CPUs)
  busiest process   : $HOG

Throughput taken on a loaded box is not a smaller number, it is a MEANINGLESS
one -- and it will be quoted later as though it were real. Wait for the box to
drain, or re-run with --dry-run to exercise the harness without measuring.

Raise the bar with MAX_LOAD=n, or override with --force-busy only if you
intend to DISCARD the result.
MSG
    exit 3
fi

# ── Guard 1: what engine is actually in each binary? ─────────────────────────
engine_of() {
    local bin="$1"
    [ -x "$bin" ] || { echo "MISSING"; return; }
    local n
    n=$(nm "$bin" 2>/dev/null | grep -c " T js_jit_" || true)
    if [ "${n:-0}" -gt 0 ]; then echo "JIT-linked ($n js_jit_* symbols)"
    else echo "no-JIT"; fi
}

echo "=== binaries ==="
printf '  %-8s %s\n    %s\n' "jit" "$JIT_BIN" "$(engine_of "$JIT_BIN")"
[ -n "$INTERP_BIN" ] && printf '  %-8s %s\n    %s\n' "interp" "$INTERP_BIN" "$(engine_of "$INTERP_BIN")"
[ -n "$HANDC_BIN" ]  && printf '  %-8s %s\n    %s\n' "handc" "$HANDC_BIN" "$(engine_of "$HANDC_BIN")"

if [ -n "$INTERP_BIN" ] && [ "$(engine_of "$INTERP_BIN")" != "no-JIT" ]; then
    cat >&2 <<MSG

REFUSING: the binary given as --interp has the JIT linked, so an arm labelled
"interpreted" would in fact measure the JIT and invert the conclusion. Build a
non-JIT engine first:
    make -C quickjs clean && make -C quickjs libquickjs.a
    auto/configure --builddir=objs_interp ... && make -f objs_interp/Makefile
MSG
    exit 4
fi

# ── configs ──────────────────────────────────────────────────────────────────
rm -rf "$WORK"; mkdir -p "$WORK/logs"
cp "$ROOT/mirror/lib/mirror.js" "$WORK/mirror.js"
cp "$HERE/../maxim_m1/maxim_mirror_app.js" "$WORK/app.js"
cp "$HERE/app_aot.js" "$WORK/app_aot.js"

skeleton() {  # $1 = extra top-level lines, $2 = location body
    cat <<CONF
worker_processes 4;
daemon on;
pid $WORK/nginx.pid;
error_log $WORK/logs/error.log crit;
$1
events { worker_connections 8192; }
http {
  access_log off;
  default_type text/plain;
  map \$http_x_tenant \$tenant_seen { ""  "-"; default \$http_x_tenant; }
  server {
    listen $PORT reuseport;
    location / { $2 }
  }
}
CONF
}

skeleton "" 'return 200 "ok\n";'                                  > "$WORK/floor.conf"
skeleton "" 'add_header x-tenant-seen $tenant_seen always; return 200 "ok\n";' \
                                                                  > "$WORK/directives.conf"
skeleton "js_source $WORK/mirror.js;
js_source $WORK/app.js;" ""                                       > "$WORK/js.conf"
skeleton "js_source $WORK/mirror.js;
js_source $WORK/app_aot.js;" ""                                   > "$WORK/js_aot.conf"

# ── run one arm ──────────────────────────────────────────────────────────────
# run_arm NAME BIN CONF EXPECT
#
# EXPECT is a regex every arm's probe response MUST match, so an arm that has
# silently stopped doing its work cannot post a flattering number. A policy
# that no longer runs looks exactly like a very fast policy.
run_arm() {
    local name="$1" bin="$2" conf="$3" expect="${4:-}"
    [ -x "$bin" ] || { printf '  %-11s SKIP (no binary)\n' "$name"; return; }

    "$bin" -c "$conf" -p "$WORK" >/dev/null 2>&1 || {
        printf '  %-11s FAILED TO START\n' "$name"
        tail -3 "$WORK/logs/error.log" 2>/dev/null | sed 's/^/      /'
        return
    }
    sleep 1

    local body probe
    body=$(curl -s -i -m 5 -H 'x-tenant: acme' "http://127.0.0.1:$PORT/" 2>/dev/null || true)
    probe=$(printf '%s' "$body" | head -1 | grep -o '[0-9][0-9][0-9]' | head -1)
    if [ "${probe:-000}" != "200" ]; then
        printf '  %-11s BAD PROBE (%s) — arm not serving, not measuring\n' "$name" "${probe:-000}"
        [ -f "$WORK/nginx.pid" ] && kill -QUIT "$(cat "$WORK/nginx.pid")" 2>/dev/null
        sleep 1; return
    fi

    if [ -n "$expect" ] && ! printf '%s' "$body" | grep -qiE "$expect"; then
        printf '  %-11s NOT DOING ITS WORK (no /%s/ in response) — refusing to measure\n' \
               "$name" "$expect"
        [ -f "$WORK/nginx.pid" ] && kill -QUIT "$(cat "$WORK/nginx.pid")" 2>/dev/null
        sleep 1; return
    fi

    if [ "$DRY_RUN" = "1" ]; then
        printf '  %-11s OK (probe 200, engine: %s)\n' "$name" "$(engine_of "$bin")"
    else
        "$WRK" -t"$THREADS" -c"$CONNS" -d"${WARMUP}s" -H 'x-tenant: acme' \
               "http://127.0.0.1:$PORT/" >/dev/null 2>&1 || true
        local best=0 i rps out
        for i in $(seq 1 "$RUNS"); do
            out=$("$WRK" -t"$THREADS" -c"$CONNS" -d"${DURATION}s" -H 'x-tenant: acme' \
                  "http://127.0.0.1:$PORT/" 2>/dev/null)
            rps=$(printf '%s' "$out" | awk '/Requests\/sec/{print int($2)}')
            rps=${rps:-0}
            [ "$rps" -gt "$best" ] && best=$rps
            sleep 2
        done
        # Service time DERIVED from the rate (Little's law: conns / rate),
        # not parsed from wrk's "Latency Avg".  A single stalled run poisons
        # that average -- observed: floor reported 25.16ms while its own rate
        # of 1,047,236 req/s implies 95us, and every other arm's parsed value
        # matched its derived one.  The derived figure cannot be skewed by an
        # outlier, needs no unit parsing, and is what the guard below wants:
        # per-request cost at a fixed concurrency.
        local svc_us=0
        [ "$best" -gt 0 ] && svc_us=$(( CONNS * 1000000 / best ))
        printf '  %-11s %10d req/s  (%d us/req at c=%d)\n' "$name" "$best" "$svc_us" "$CONNS"
        echo "$name $best $svc_us" >> "$WORK/results.txt"
    fi

    [ -f "$WORK/nginx.pid" ] && kill -QUIT "$(cat "$WORK/nginx.pid")" 2>/dev/null
    sleep 1
}

echo
echo "=== arms ($([ "$DRY_RUN" = 1 ] && echo 'dry run — no load' || echo "${RUNS}x${DURATION}s, best-of")) ==="
: > "$WORK/results.txt"

# NOTE on the directives arm: it emits x-tenant-seen but CANNOT do the shared
# counter, so it does strictly less work than the JS arms. M1 had the same
# asymmetry and used it the same way -- as a "how fast is nginx alone at the
# header part" reference, NOT as an equal-work comparison. Do not read
# directives-vs-jit as apples to apples.
run_arm "floor"      "$JIT_BIN"    "$WORK/floor.conf"       ""
run_arm "directives" "$JIT_BIN"    "$WORK/directives.conf"  "x-tenant-seen: acme"
run_arm "jit"        "$JIT_BIN"    "$WORK/js.conf"          "x-count:.*|x-tenant-seen: acme"
run_arm "jit-aot"    "$JIT_BIN"    "$WORK/js_aot.conf"    "x-count:.*|x-tenant-seen: acme"
[ -n "$INTERP_BIN" ] && run_arm "interp" "$INTERP_BIN" "$WORK/js.conf" "x-count:"
[ -n "$HANDC_BIN" ]  && run_arm "handc"  "$HANDC_BIN"  "$WORK/directives.conf" "x-count:"

# ── GUARD 4: is the SERVER the bottleneck? ───────────────────────────────────
#
# The three guards above (right engine, arm doing its work, idle box) all pass
# happily while the machine is in a regime where these numbers mean nothing.
#
# Comparing arms only says something about the policy if nginx is what limits
# throughput. If a fixed per-request cost outside nginx dominates -- a firewall
# in the loopback path is the one that bites here -- then every arm pays it,
# nginx never saturates, and ALL RATIOS COMPRESS TOWARD 1.0. The harness then
# reports "little headroom, M4/M5 hard to justify", which is a multi-week
# decision drawn from a network setting.
#
# Caught 2026-09-11: floor 111k req/s at 840us latency with workers at ~50% CPU,
# against 1,190,964 req/s from the same harness on the same box three days
# earlier. Cause was .wslconfig networkingMode=mirrored + firewall=true, the
# pitfall already documented in CLAUDE.md -- documented, and still missed,
# because nothing checked.
#
# Loopback on an unencumbered box is tens of microseconds. Anything near a
# millisecond means the request is not spending its time in nginx.
FLOOR_LAT_MAX_US="${FLOOR_LAT_MAX_US:-250}"
if [ "$DRY_RUN" = "0" ] && [ -s "$WORK/results.txt" ]; then
    floor_lat=$(awk '$1=="floor"{print $3}' "$WORK/results.txt")   # derived, see run_arm
    if [ -n "${floor_lat:-}" ] && [ "${floor_lat:-0}" -gt "$FLOOR_LAT_MAX_US" ]; then
        echo
        echo "REFUSING TO REPORT: the server is not the bottleneck."
        echo
        printf '  floor cost    : %s us/req at c=%s   (max %s us)\n' \
               "$floor_lat" "$CONNS" "$FLOOR_LAT_MAX_US"
        printf '  floor rate    : %s req/s\n' "$(awk '$1=="floor"{print $2}' "$WORK/results.txt")"
        echo
        echo "Loopback should be tens of microseconds. At this latency nginx is not"
        echo "saturated, every arm pays the same large fixed cost, and the RATIOS"
        echo "BETWEEN ARMS COMPRESS TOWARD 1.0 -- so a small measured headroom would"
        echo "be an artifact of the network path, not a fact about the JIT."
        echo
        echo "On WSL2 this is almost always .wslconfig:"
        echo "    networkingMode=mirrored + firewall=true  routes loopback through"
        echo "    Windows Defender Firewall (~7-10x throughput drop)."
        echo "Switch to NAT mode, 'wsl --shutdown', and re-run."
        echo
        echo "Raw per-arm numbers (NOT a result, do not quote):"
        awk '{printf "    %-11s %10d req/s  %6d us/req\n", $1, $2, $3}' "$WORK/results.txt"
        exit 3
    fi
fi

# ── verdict ──────────────────────────────────────────────────────────────────
if [ "$DRY_RUN" = "0" ] && [ -s "$WORK/results.txt" ]; then
    echo
    echo "=== result ==="
    awk '
      { v[$1] = $2 }
      END {
        f = v["floor"]
        printf "  %-11s %10s  %8s\n", "arm", "req/s", "% floor"
        for (k in v) if (k == "floor") printf "  %-11s %10d  %7.0f%%\n", k, v[k], 100
        for (k in v) if (k != "floor") printf "  %-11s %10d  %7.0f%%\n", k, v[k], (f ? 100*v[k]/f : 0)
        print ""
        if (v["jit"] && f) {
          head = f / v["jit"]
          printf "  headroom above the JIT, to the machine ceiling: %.2fx\n", head
          print  "  (M1 measured hand-C at 90-96% of floor, so the compiler"
          print  "   ceiling is just under this.)"
          print  ""
          if (head < 1.3)
            print "  READS AS: little left to reclaim — M4/M5 hard to justify."
          else if (head < 2.0)
            print "  READS AS: moderate headroom — worth a closer look."
          else
            print "  READS AS: large headroom — the M1 conclusion likely still stands."
          print  "  This is ONE box and ONE policy shape; treat it as a signal."
        }
      }' "$WORK/results.txt"
fi
