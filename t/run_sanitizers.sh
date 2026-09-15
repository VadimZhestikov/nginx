#!/usr/bin/env bash
# M-SES S6 — run the COMCON corpus under ASAN and UBSAN.
#
# The normal suites print "ok - no sanitizer errors" on every run, and that line
# is VACUOUS: neither objs/ nor objs_jit/ is built with a sanitizer, so the
# check has nothing to look at. This script is what makes it mean something.
#
# THREE GUARDS, because a clean sanitizer run is the easiest false negative
# there is -- an inert build reports exactly the same "no findings" as a clean one:
#
#   1. the binary must actually carry the sanitizer's symbols;
#   2. a POSITIVE CONTROL must land a report through the SAME prove pipeline
#      (leak detection on one file), proving reports can reach the log at all;
#   3. findings are split into src/js (ours, fatal) and everything else
#      (upstream nginx -- reported, not failed).
#
# Usage:  bash t/run_sanitizers.sh [glob]      default glob: comcon_*.t
set -uo pipefail
cd "$(dirname "$0")/.."
GLOB="${1:-comcon_*.t}"
rc=0

# Expand here, not in prove: prove does not glob its arguments, so passing the
# pattern through as one quoted string makes it error out, run NOTHING, and
# report zero findings -- which this script would have called a PASS. That
# happened on the first run of this file.
shopt -s nullglob
FILES=( t/$GLOB )
shopt -u nullglob
if [ "${#FILES[@]}" -eq 0 ]; then
    echo "no test files match t/$GLOB"; exit 2
fi
echo "corpus: ${#FILES[@]} file(s) matching t/$GLOB"

need_build() {  # name, builddir, symbol
    if [ ! -x "$2/nginx" ]; then
        echo "MISSING $2/nginx — configure it first (see the recipe in the"
        echo "  technique-nginx-asan-build memory, or objs_asan/ngx_auto_config.h"
        echo "  which records the exact configure line)."
        return 1
    fi
    local n; n=$(nm "$2/nginx" 2>/dev/null | grep -c "$3")
    if [ "${n:-0}" -lt 1 ]; then
        echo "REFUSING: $2/nginx carries no $3 symbols — it is not a $1 build,"
        echo "  and a clean run from it would prove nothing."
        return 1
    fi
    printf '  %-6s %s (%s symbols)\n' "$1" "$2/nginx" "$n"
}

echo "=== binaries ==="
need_build ASAN  objs_asan  __asan_  || exit 2
need_build UBSAN objs_ubsan __ubsan  || exit 2

echo
echo "=== positive control: can a report reach the log through prove? ==="
rm -f /tmp/s6_ctl.*
ASAN_OPTIONS="detect_leaks=1:log_path=/tmp/s6_ctl:abort_on_error=0" \
  TEST_NGINX_BINARY="$PWD/objs_asan/nginx" prove t/comcon_admit.t >/dev/null 2>&1
if [ "$(ls /tmp/s6_ctl.* 2>/dev/null | wc -l)" -lt 1 ]; then
    echo "  REFUSING: the control produced no report, so a clean corpus run"
    echo "  below would be meaningless. Check ASAN_OPTIONS/log_path plumbing."
    exit 2
fi
echo "  ok — leak detection landed a report, the pipeline works"

run_one() {  # name, builddir, envvar, prefix
    local name="$1" dir="$2" var="$3" pfx="$4" out
    echo
    echo "=== $name over ${#FILES[@]} file(s) ==="
    rm -f "$pfx".*
    out=$(env "$var=print_stacktrace=1:log_path=$pfx:halt_on_error=0:detect_leaks=0" \
              TEST_NGINX_BINARY="$PWD/$dir/nginx" prove "${FILES[@]}" 2>&1)
    echo "$out" | grep -E "^(Files=|Result:)" | sed 's/^/  /'

    # WORK VERIFICATION, the guard this script was missing on its first run:
    # "no findings" is only meaningful alongside "N assertions actually ran".
    # Zero findings from a run that executed nothing is the same output as a
    # clean one, which is the whole failure mode this file exists to prevent.
    local tests
    tests=$(echo "$out" | sed -nE 's/.*Tests=([0-9]+).*/\1/p' | tail -1)
    if [ "${tests:-0}" -lt 1 ]; then
        echo "  REFUSING: $name run executed NO tests — zero findings here means"
        echo "  nothing was exercised, not that nothing is wrong."
        echo "$out" | tail -5 | sed 's/^/    /'
        rc=1
        return
    fi
    echo "  tests run          : $tests"

    # A file skipping with "no js module" means nginx COULD NOT START, so that
    # file was not exercised at all. This is how a fatal-on-first-finding flag
    # such as -fno-sanitize-recover silently empties a whole run: stock nginx
    # trips UB in ngx_pstrdup at startup, dies, and all 34 files skip while the
    # harness still prints "All tests successful".
    # Other skip reasons are the test's own business (a missing second binary,
    # an absent module) and are reported, not failed.
    local dead other_skips
    dead=$(echo "$out" | grep -c "skipped: no js module" || true)
    other_skips=$(echo "$out" | grep "skipped:" | grep -vc "no js module" || true)
    if [ "${dead:-0}" -gt 0 ]; then
        echo "  REFUSING: $dead file(s) skipped because nginx could not start"
        echo "  under $name — those files were not exercised."
        rc=1
    fi
    [ "${other_skips:-0}" -gt 0 ] && \
        echo "  skipped (own reasons): $other_skips  — not a failure"

    local ours other
    ours=$(cat "$pfx".* 2>/dev/null | grep -cE "^src/js/" || true)
    other=$(cat "$pfx".* 2>/dev/null | grep -E "runtime error|ERROR: " \
            | grep -vcE "^src/js/" || true)
    echo "  findings in src/js : ${ours:-0}"
    echo "  findings elsewhere : ${other:-0}  (upstream nginx; reported, not failed)"
    if [ "${ours:-0}" -gt 0 ]; then
        echo "  --- ours:"
        cat "$pfx".* 2>/dev/null | grep -E "^src/js/" | sort -u | sed 's/^/    /' | head -20
        rc=1
    fi
    cat "$pfx".* 2>/dev/null | grep -oE "^[a-z/_.]+\.c:[0-9]+" | sort | uniq -c \
        | sort -rn | head -5 | sed 's/^/    site: /'
}

# ---------------------------------------------------------------------------
# A LEAK STAGE, because the corpus runs above are detect_leaks=0.
#
# They have to be: a full leak report on this tree is dominated by one-off
# allocations that live for the process (the SharedWorker manager thread's 16
# bytes, for one), so leaks-on over the whole corpus would be noise nobody reads.
# But that left the tree with NO leak instrument at all, and it cost something:
# P17's `conn.reject()` closed a freshly accepted connection with
# ngx_close_connection(), which does not destroy c->pool -- ngx_event_accept()
# had just created it -- so every rejected connection leaked 512 bytes.  Found
# only by running leaks-on by hand.
#
# So this stage is narrow on purpose: the two tests that drive a connection
# through src/js's own pre-http teardown paths, and an assertion that greps for a
# leak whose allocation stack lands in ngx_event_accept.  That is specific enough
# to ignore the pre-existing noise and still fail the day one of those paths
# forgets the pool again.
#
# THE GENERAL RULE THIS STAGE GUARDS.  Between ngx_event_accept() and
# ngx_http_init_connection() the connection pool has no other owner, so any code
# of ours that closes a connection in that window must use
# ngx_http_close_connection().  There are two such windows -- the accept hook's
# reject path, and the L4 filter window -- and BOTH had it wrong: 13 call sites.
#
# AN RSS TEST WAS TRIED FIRST AND THROWN AWAY: with the fix reverted, 3000
# rejected connections moved worker RSS by 0 KB, because 1.5 MB of leaked
# 512-byte chunks comes out of heap the warm-up had already mapped.  Its control
# did not fire, so it proved nothing.
# ---------------------------------------------------------------------------

echo
echo "=== leak stage: a connection closed before http init must not leak its pool ==="
rm -f /tmp/s6_leak.*
env ASAN_OPTIONS="detect_leaks=1:log_path=/tmp/s6_leak:abort_on_error=0" \
    TEST_NGINX_BINARY="$PWD/objs_asan/nginx" \
    prove t/js_pilgrim_p17_reject_leak.t t/js_pilgrim_p17_l4_window.t >/dev/null 2>&1
# Parse leak BLOCKS, not lines.  The first version grepped -B1 around the
# ngx_event_accept frame, but "in N object(s)" is the block HEADER four frames
# above it -- so it summed nothing and reported clean with the fix reverted.  Its
# control not firing is the only reason that was caught.
leak_objs=$(cat /tmp/s6_leak.* 2>/dev/null | awk '
    /^(Direct|Indirect) leak of/ { hdr = $0; inblk = 1; hit = 0; next }
    inblk && /ngx_event_accept/  { hit = 1 }
    inblk && /^[[:space:]]*$/    { if (hit) print hdr; inblk = 0 }
    END                          { if (inblk && hit) print hdr }
  ' | grep -oE "in [0-9]+ object" | grep -oE "[0-9]+" | paste -sd+ - | bc 2>/dev/null)
leak_objs=${leak_objs:-0}
if [ "$(ls /tmp/s6_leak.* 2>/dev/null | wc -l)" -lt 1 ]; then
    echo "  REFUSING: leaks-on produced no report at all, so a clean result here"
    echo "  would be meaningless (the same guard the control above applies)."
    rc=1
elif [ "$leak_objs" -gt 0 ]; then
    echo "  FAIL  $leak_objs connection pool(s) leaked from ngx_event_accept --"
    echo "        something in src/js is closing a connection BEFORE"
    echo "        ngx_http_init_connection() with ngx_close_connection(), which"
    echo "        does not destroy c->pool.  Use ngx_http_close_connection()."
    rc=1
else
    echo "  ok — 300 rejects + the L4 window paths, no pool leaked from accept"
fi

run_one ASAN  objs_asan  ASAN_OPTIONS  /tmp/s6_asan
run_one UBSAN objs_ubsan UBSAN_OPTIONS /tmp/s6_ubsan

echo
[ "$rc" = 0 ] && echo "S6 sanitizer gate: PASS (no findings in src/js)" \
              || echo "S6 sanitizer gate: FAIL"
exit "$rc"
