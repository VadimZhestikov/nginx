#!/usr/bin/env bash
# bench.sh — Pilgrim vs njs performance comparison
#
# Usage:
#   bench.sh [options]
#
# Options:
#   --pilgrim=PATH      path to Pilgrim nginx binary  (default: ../objs/nginx)
#   --njs=PATH          path to njs-enabled nginx binary (default: skip njs tests)
#   --duration=N        wrk test duration in seconds  (default: 30)
#   --connections=N     wrk concurrent connections    (default: 50)
#   --threads=N         wrk threads                   (default: 4)
#   --warmup=N          warmup duration in seconds    (default: 5)
#   --runs=N            number of wrk runs per scenario (default: 1)
#   --cooldown=N        pause between consecutive runs, seconds (default: 2)
#   --server-cpus=LIST  taskset CPU list for nginx  (e.g. 4 or 0-1)
#   --client-cpus=LIST  taskset CPU list for wrk    (e.g. 5 or 2-3)
#
# No CPU auto-pinning: both nginx and wrk run on all available CPUs.
# Pinning wrk to isolated CPUs and nginx elsewhere causes cross-CPU wakeup
# overhead on loopback (expensive IPIs between CPU domains) that cuts
# throughput more than it helps stability.  Use --server-cpus/--client-cpus
# only if nginx and wrk can share the same isolated CPU set.
#
# Example:
#   bench.sh --njs=njs-nginx/objs/nginx --runs=10
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── defaults ──────────────────────────────────────────────────────────────────
PILGRIM_NGINX="${PILGRIM_NGINX:-$SCRIPT_DIR/../objs/nginx}"
NJS_NGINX="${NJS_NGINX:-}"
DURATION=30
CONNECTIONS=50
THREADS=4
WARMUP=15
RUNS=1
COOLDOWN=2
SERVER_CPUS=""
CLIENT_CPUS=""

PILGRIM_PORT=9001
NJS_PORT=9011

# ── argument parsing ──────────────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --pilgrim=*)     PILGRIM_NGINX="${arg#*=}" ;;
        --njs=*)         NJS_NGINX="${arg#*=}" ;;
        --duration=*)    DURATION="${arg#*=}" ;;
        --connections=*) CONNECTIONS="${arg#*=}" ;;
        --threads=*)     THREADS="${arg#*=}" ;;
        --warmup=*)      WARMUP="${arg#*=}" ;;
        --runs=*)        RUNS="${arg#*=}" ;;
        --cooldown=*)    COOLDOWN="${arg#*=}" ;;
        --server-cpus=*) SERVER_CPUS="${arg#*=}" ;;
        --client-cpus=*) CLIENT_CPUS="${arg#*=}" ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

# ── CPU pinning ───────────────────────────────────────────────────────────────
# Expand a cpulist string ("2-5" or "2,3,4,5") to one CPU-number per line.
_expand_cpulist() {
    echo "$1" | tr ',' '\n' | awk -F- '{
        if (NF == 2) { for (i = $1; i <= $2; i++) print i }
        else         { print $1 }
    }'
}

# CPU pinning is opt-in: pass --server-cpus / --client-cpus explicitly.
# Auto-pinning wrk to isolated CPUs causes cross-CPU wakeup overhead on
# loopback benchmarks (nginx and wrk on different CPU domains → expensive IPIs).

# Build taskset prefix arrays (empty = no pinning).
TASKSET_SERVER=()
TASKSET_CLIENT=()
if [ -n "$SERVER_CPUS" ] && command -v taskset &>/dev/null; then
    TASKSET_SERVER=(taskset -c "$SERVER_CPUS")
fi
if [ -n "$CLIENT_CPUS" ] && command -v taskset &>/dev/null; then
    TASKSET_CLIENT=(taskset -c "$CLIENT_CPUS")
fi

# ── pre-flight checks ─────────────────────────────────────────────────────────
WRK="${WRK:-wrk}"
if ! command -v "$WRK" &>/dev/null; then
    if [ -x "/tmp/wrk-src/wrk" ]; then
        WRK="/tmp/wrk-src/wrk"
    else
        echo "ERROR: wrk not found. Install it: sudo apt install wrk"
        exit 1
    fi
fi

if [ ! -x "$PILGRIM_NGINX" ]; then
    echo "ERROR: Pilgrim binary not found: $PILGRIM_NGINX"
    echo "  Build it from the repo root: make -j\$(nproc)"
    exit 1
fi

# ── helpers ───────────────────────────────────────────────────────────────────
RESULTS_FILE="$SCRIPT_DIR/results.txt"
> "$RESULTS_FILE"

start_nginx() {
    local dir="$1" binary="$2"
    mkdir -p "$dir/logs"
    "$binary" -p "$dir" -c nginx.conf -s stop 2>/dev/null || true
    sleep 0.3
    "${TASKSET_SERVER[@]+"${TASKSET_SERVER[@]}"}" "$binary" -p "$dir" -c nginx.conf
    sleep 0.5
}

stop_nginx() {
    local dir="$1" binary="$2"
    "$binary" -p "$dir" -c nginx.conf -s stop 2>/dev/null || true
    sleep 0.3
}

# _filter_outliers: reads "key value" or plain "value" lines from stdin,
# drops values outside [0.3×median, 2×median], prints kept values one per line.
# Used by both stats() and avg_for() so wrk timing glitches don't skew results.
_filter_outliers() {
    awk '
        { v[NR] = ($2 != "" ? $2 : $1) }
        END {
            n = NR
            for (i = 1; i <= n; i++) s[i] = v[i]
            for (i = 1; i <= n; i++)
                for (j = i+1; j <= n; j++)
                    if (s[j] < s[i]) { t=s[i]; s[i]=s[j]; s[j]=t }
            median = (n % 2 == 1) ? s[int(n/2)+1] : (s[n/2]+s[n/2+1])/2
            lo = 0.3 * median; hi = 2.0 * median
            for (i = 1; i <= n; i++)
                if (v[i] >= lo && v[i] <= hi) print v[i]
        }
    '
}

# stats <val1> <val2> ... — prints "avg=N min=N max=N sd=N cv=N% [K outlier(s) dropped]"
stats() {
    local all dropped kept
    all=$(printf '%s\n' "$@")
    kept=$(echo "$all" | _filter_outliers)
    dropped=$(( $# - $(echo "$kept" | grep -c .) ))

    echo "$kept" | awk -v dropped="$dropped" '
        { v[NR] = $1; sum += $1 }
        END {
            n   = NR
            avg = sum / n
            min = v[1]; max = v[1]
            for (i = 2; i <= n; i++) {
                if (v[i] < min) min = v[i]
                if (v[i] > max) max = v[i]
            }
            for (i = 1; i <= n; i++) sq += (v[i] - avg)^2
            sd = sqrt(sq / n)
            cv = (avg > 0) ? sd / avg * 100 : 0
            if (dropped > 0)
                printf "avg=%d  min=%d  max=%d  sd=%.0f  cv=%.1f%%  [%d outlier(s) dropped]\n",
                       avg, min, max, sd, cv, dropped
            else
                printf "avg=%d  min=%d  max=%d  sd=%.0f  cv=%.1f%%\n",
                       avg, min, max, sd, cv
        }
    '
}

# run_scenario <label> <key> <url>
# Runs wrk RUNS times; prints per-run lines and a stats summary when RUNS > 1.
# Appends "<key> <rps>" lines to RESULTS_FILE (one per run).
run_scenario() {
    local label="$1" key="$2" url="$3"
    local values=()

    for ((i = 1; i <= RUNS; i++)); do
        [ "$i" -gt 1 ] && sleep "$COOLDOWN"

        local out rps lat
        out=$("${TASKSET_CLIENT[@]+"${TASKSET_CLIENT[@]}"}" \
              "$WRK" -t "$THREADS" -c "$CONNECTIONS" -d "${DURATION}s" "$url" 2>&1)
        rps=$(echo "$out" | grep "Requests/sec:" | awk '{printf "%.0f", $2}')
        lat=$(echo "$out"  | grep "Latency"       | awk '{print $2}')

        if [ "$RUNS" -eq 1 ]; then
            printf "  %-42s %8s req/s   latency avg %s\n" "$label" "$rps" "$lat"
        else
            printf "  %-42s  [%2d/%d] %8s req/s   lat %s\n" \
                   "$label" "$i" "$RUNS" "$rps" "$lat"
        fi

        values+=("$rps")
        echo "$key $rps" >> "$RESULTS_FILE"
    done

    if [ "$RUNS" -gt 1 ]; then
        local s
        s=$(stats "${values[@]}")
        printf "  %-42s  └─ %s\n\n" "" "$s"
    fi
}

run_warmup() {
    "${TASKSET_CLIENT[@]+"${TASKSET_CLIENT[@]}"}" \
        "$WRK" -t "$THREADS" -c "$CONNECTIONS" -d "${WARMUP}s" "$1" &>/dev/null
}

# ── banner ────────────────────────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════════╗"
echo "║     Pilgrim vs njs — HTTP handler throughput             ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
printf "  wrk: %d threads / %d connections / %ds per run" \
       "$THREADS" "$CONNECTIONS" "$DURATION"
[ "$RUNS" -gt 1 ] && printf " × %d runs (cooldown %ds)" "$RUNS" "$COOLDOWN"
echo ""
echo "  Pilgrim: $PILGRIM_NGINX"
[ -n "$NJS_NGINX" ] && echo "  njs:     $NJS_NGINX"
if [ -n "$SERVER_CPUS" ] || [ -n "$CLIENT_CPUS" ]; then
    echo "  CPU pin: nginx→[${SERVER_CPUS:-all}]  wrk→[${CLIENT_CPUS:-all}]"
else
    echo "  CPU pin: none (use --server-cpus / --client-cpus to enable pinning)"
fi
echo ""

# ── Pilgrim tests ─────────────────────────────────────────────────────────────
echo "── Pilgrim ──────────────────────────────────────────────────"

start_nginx "$SCRIPT_DIR/pilgrim" "$PILGRIM_NGINX"

echo "  Warming up (${WARMUP}s)..."
run_warmup "http://127.0.0.1:$PILGRIM_PORT/baseline"

run_scenario "baseline  (return 200, no JS)"        pilgrim_baseline  "http://127.0.0.1:$PILGRIM_PORT/baseline"
run_scenario "empty     (JS handler, respond ok)"   pilgrim_empty     "http://127.0.0.1:$PILGRIM_PORT/empty"
run_scenario "headers   (JS handler, dump headers)" pilgrim_headers   "http://127.0.0.1:$PILGRIM_PORT/headers"

stop_nginx "$SCRIPT_DIR/pilgrim" "$PILGRIM_NGINX"

# ── njs tests ─────────────────────────────────────────────────────────────────
if [ -z "$NJS_NGINX" ]; then
    echo ""
    echo "── njs ──────────────────────────────────────────────────────"
    echo "  (skipped — pass --njs=/path/to/njs-nginx to enable)"
elif [ ! -x "$NJS_NGINX" ]; then
    echo ""
    echo "── njs ──────────────────────────────────────────────────────"
    echo "  (skipped — binary not executable: $NJS_NGINX)"
else
    echo ""
    echo "── njs ──────────────────────────────────────────────────────"

    start_nginx "$SCRIPT_DIR/njs" "$NJS_NGINX"

    echo "  Warming up (${WARMUP}s)..."
    run_warmup "http://127.0.0.1:$NJS_PORT/baseline"

    run_scenario "baseline  (return 200, no JS)"        njs_baseline  "http://127.0.0.1:$NJS_PORT/baseline"
    run_scenario "empty     (js_content, return ok)"    njs_empty     "http://127.0.0.1:$NJS_PORT/empty"
    run_scenario "headers   (js_content, headersIn)"    njs_headers   "http://127.0.0.1:$NJS_PORT/headers"

    stop_nginx "$SCRIPT_DIR/njs" "$NJS_NGINX"
fi

# ── summary table ─────────────────────────────────────────────────────────────
# For each key, compute the outlier-filtered average from results.txt.
avg_for() {
    local key="$1"
    grep "^$key " "$RESULTS_FILE" 2>/dev/null \
    | _filter_outliers \
    | awk '{sum+=$1; n++} END { if(n>0) printf "%d", sum/n; else print "N/A" }' \
    || echo "N/A"
}

p_base=$(avg_for pilgrim_baseline)
p_empty=$(avg_for pilgrim_empty)
p_headers=$(avg_for pilgrim_headers)
n_base=$(avg_for njs_baseline)
n_empty=$(avg_for njs_empty)
n_headers=$(avg_for njs_headers)

echo ""
echo "── Summary$([ "$RUNS" -gt 1 ] && echo " (averages over $RUNS runs)" || echo " (req/s)") ──────────────────────"
printf "\n  %-36s  %10s  %10s\n" "scenario" "Pilgrim" "njs"
printf "  %-36s  %10s  %10s\n"   "--------" "-------" "---"
printf "  %-36s  %10s  %10s\n"   "baseline  (return 200, no JS)"  "$p_base"    "$n_base"
printf "  %-36s  %10s  %10s\n"   "empty     (minimal JS handler)" "$p_empty"   "$n_empty"
printf "  %-36s  %10s  %10s\n"   "headers   (JS + object access)" "$p_headers" "$n_headers"

# JS overhead ratio
is_num() { [[ "$1" =~ ^[0-9]+$ ]]; }

if is_num "$p_base" && is_num "$p_empty"; then
    pct=$(awk "BEGIN { printf \"%.0f\", (1 - $p_empty/$p_base) * 100 }")
    printf "\n  Pilgrim empty-handler overhead vs baseline: ~%s%%\n" "$pct"
fi
if is_num "$n_base" && is_num "$n_empty"; then
    pct=$(awk "BEGIN { printf \"%.0f\", (1 - $n_empty/$n_base) * 100 }")
    printf "  njs     empty-handler overhead vs baseline: ~%s%%\n" "$pct"
fi

echo ""
echo "  Raw samples: $RESULTS_FILE"
echo ""
