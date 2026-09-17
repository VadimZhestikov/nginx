#!/usr/bin/env bash
# Shared harness for the js_comcon_demos test scripts.
#
#   . "$(dirname "$0")/../../lib/demo.sh"     # from a demo's test.sh
#   demo_start [binary]                       # starts nginx -p . -c nginx.conf
#   check "desc" "needle" "$haystack"         # substring present  -> PASS
#   absent "desc" "needle" "$haystack"        # substring absent   -> PASS
#   get /path  [curl args...]                 # curl -s with headers
#   demo_end                                  # prints the tally, exits 1 on FAIL
#
# The binary: $NGINX if set, else objs/nginx at the repo root (three levels up
# from a demo directory).  Each demo has its own port, so demos never collide,
# but the harness still stops the instance it started on exit.
set -uo pipefail

DEMO_DIR="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
ROOT="$(cd "$DEMO_DIR/../../.." && pwd)"
NGINX="${NGINX:-$ROOT/objs/nginx}"
PASS=0; FAIL=0

cd "$DEMO_DIR"
mkdir -p logs; : > logs/error.log

# Stop through the pid file, not `nginx -s stop`: the -s form re-parses the
# config, which re-runs js_source, whose createSocket() cannot bind the port
# the running master holds -- the parse fails and the signal is never sent.
_demo_cleanup() {
    local pid; pid=$(cat logs/nginx.pid 2>/dev/null) || return 0
    kill -QUIT "$pid" 2>/dev/null || return 0
    for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$pid" 2>/dev/null || return 0; sleep 0.2; done
    kill -TERM "$pid" 2>/dev/null || true
}
trap _demo_cleanup EXIT

demo_start() {
    [ -n "${1:-}" ] && NGINX="$1"
    if [ ! -x "$NGINX" ]; then
        echo "no nginx binary at $NGINX (build objs/ first, or set NGINX=...)"; exit 2
    fi
    # `env -u NGINX`: nginx reads a variable of that name as its inherited
    # listening sockets (binary upgrade), refuses to daemonize and blocks
    # this shell -- so the harness's own variable must not reach it.
    if ! env -u NGINX "$NGINX" -p . -c nginx.conf; then
        echo "nginx failed to start; logs/error.log:"; sed 's/^/    /' logs/error.log; exit 2
    fi
    sleep 0.6
}

demo_stop() { _demo_cleanup; sleep 0.3; }

check() {   # desc, needle, haystack
    if grep -qF -- "$2" <<<"$3"; then
        echo "  PASS: $1"; PASS=$((PASS+1))
    else
        echo "  FAIL: $1"; echo "        wanted [$2] in:"; sed 's/^/          /' <<<"$3"
        FAIL=$((FAIL+1))
    fi
}
absent() {  # desc, forbidden needle, haystack
    if grep -qF -- "$2" <<<"$3"; then
        echo "  FAIL: $1 (found forbidden [$2])"; sed 's/^/          /' <<<"$3"; FAIL=$((FAIL+1))
    else
        echo "  PASS: $1"; PASS=$((PASS+1))
    fi
}
get() {     # path [curl args...]  -> headers + body
    local p="$1"; shift
    curl -s --max-time 5 -D- "$@" "http://127.0.0.1:$PORT$p"
}
body() {    # path [curl args...]  -> body only
    local p="$1"; shift
    curl -s --max-time 5 "$@" "http://127.0.0.1:$PORT$p"
}
show() {    # label, text  -> echo indented, for the reader
    echo "  -- $1"; sed 's/^/     /' <<<"$2"
}

demo_end() {
    echo
    echo "==================================================="
    printf '  %s: PASS=%d FAIL=%d\n' "${DEMO_NAME:-demo}" "$PASS" "$FAIL"
    echo "==================================================="
    [ "$FAIL" -eq 0 ]
}
