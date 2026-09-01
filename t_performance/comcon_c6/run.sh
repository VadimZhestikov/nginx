#!/bin/bash
# COMCON C6 benchmark: interpreted (objs/nginx) vs AOT-compiled (objs_jit/nginx)
# on the same confined tenant handler. Build both variants first (see the
# build-and-test memory / CLAUDE.md JIT recipe). Requires wrk.
#   bash t_performance/comcon_c6/run.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$HERE/../.."
WRK="${WRK:-$(command -v wrk || echo /tmp/wrk-src/wrk)}"
PORT="${PORT:-8850}"; DUR="${DUR:-10}"
[ -x "$WRK" ] || { echo "need wrk (set WRK=/path/to/wrk)"; exit 2; }

run_one(){
  local bin=$1 handler=$2 label=$3
  cat > "$HERE/n.conf" <<CONF
daemon off; worker_processes 1; worker_cpu_affinity 0010; pid $HERE/n.pid; error_log $HERE/n.err info;
js_tenant_source $HERE/$handler.js;
events { worker_connections 1024; }
http { access_log off; keepalive_requests 1000000;
  server { listen 127.0.0.1:$PORT; location /t { js_tenant_handler; } } }
CONF
  "$ROOT/$bin/nginx" -p "$HERE" -c "$HERE/n.conf" >/dev/null 2>"$HERE/n.stderr" &
  local npid=$!; sleep 1.2
  local aot="interp"; grep -q 'lowered to native C' "$HERE/n.stderr" && aot="COMPILED"
  taskset -c 4-7 "$WRK" -t2 -c50 -d3s  "http://127.0.0.1:$PORT/t" >/dev/null 2>&1
  local out; out=$(taskset -c 4-7 "$WRK" -t2 -c50 -d"${DUR}s" "http://127.0.0.1:$PORT/t" 2>&1)
  kill -QUIT $npid 2>/dev/null; wait $npid 2>/dev/null
  printf '  %-14s %-11s %14s req/s\n' "$label" "[$aot]" "$(echo "$out"|awk '/Requests\/sec/{print $2}')"
}
for h in realistic compute; do
  echo "-- handler: $h --"
  run_one objs     "$h" interpreted
  run_one objs_jit "$h" jit-compiled
done
rm -f "$HERE"/n.conf "$HERE"/n.pid "$HERE"/n.err "$HERE"/n.stderr
