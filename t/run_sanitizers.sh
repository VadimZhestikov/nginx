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

run_one ASAN  objs_asan  ASAN_OPTIONS  /tmp/s6_asan
run_one UBSAN objs_ubsan UBSAN_OPTIONS /tmp/s6_ubsan

echo
[ "$rc" = 0 ] && echo "S6 sanitizer gate: PASS (no findings in src/js)" \
              || echo "S6 sanitizer gate: FAIL"
exit "$rc"
