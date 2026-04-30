#!/usr/bin/env bash
# run.sh — one-shot benchmark: builds wrk if needed, then runs bench.sh.
#
# Usage:
#   bash run.sh [--sessions=N] [bench.sh options...]
#
# --sessions=N   run bench.sh N times in sequence (default: 1)
#
# All other options are forwarded to bench.sh unchanged.
# Common bench.sh options:
#   --njs=PATH        compare against an njs-enabled nginx binary
#   --runs=N          wrk runs per scenario (default: 10)
#   --duration=N      seconds per run       (default: 30)
#   --warmup=N        warmup seconds        (default: 15)
#
# Examples:
#   bash run.sh --njs=njs-nginx/objs/nginx
#   bash run.sh --njs=njs-nginx/objs/nginx --sessions=4 --runs=10 --duration=15
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SESSIONS=1

BENCH_ARGS=()
for arg in "$@"; do
    case "$arg" in
        --sessions=*) SESSIONS="${arg#*=}" ;;
        *) BENCH_ARGS+=("$arg") ;;
    esac
done

# ── Ensure wrk is available ───────────────────────────────────────────────────
WRK="${WRK:-wrk}"
if ! command -v "$WRK" &>/dev/null; then
    if [ -x /tmp/wrk-src/wrk ]; then
        WRK=/tmp/wrk-src/wrk
    else
        echo "wrk not found — building from source (needs git, make, gcc, libssl-dev)..."
        git clone --depth=1 https://github.com/wg/wrk.git /tmp/wrk-src
        make -C /tmp/wrk-src WITH_OPENSSL=/usr -j"$(nproc)"
        WRK=/tmp/wrk-src/wrk
        echo "wrk built: $($WRK --version 2>&1 | head -1)"
    fi
fi
export WRK

# ── Run sessions ─────────────────────────────────────────────────────────────
for ((s = 1; s <= SESSIONS; s++)); do
    [ "$SESSIONS" -gt 1 ] && echo ""
    [ "$SESSIONS" -gt 1 ] && echo "══ Session $s / $SESSIONS ══════════════════════════════════════"
    WRK="$WRK" bash "$SCRIPT_DIR/bench.sh" "${BENCH_ARGS[@]+"${BENCH_ARGS[@]}"}"
    [ "$SESSIONS" -gt 1 ] && [ "$s" -lt "$SESSIONS" ] && sleep 3
done
