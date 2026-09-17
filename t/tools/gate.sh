#!/usr/bin/env bash
#
# THE GATE -- what "gate green" in a commit message means, as one command.
#
# Every code commit on this branch was preceded by this run; until v5.123 the
# script lived in a session's temp directory, so the claim was the author's
# word.  Now it is here, and a reader can run exactly what the message says.
#
#   bash t/tools/gate.sh              # the gate: ~15-20 min, exit 0 iff green
#   bash t/tools/gate.sh --configure  # (once per clone) configure the four builddirs
#
# THE STAGES, in order, each on a binary REBUILT FIRST:
#   0. objs_asan and objs_ubsan are rebuilt (t/run_sanitizers.sh does NOT
#      rebuild them; a stale sanitizer binary once "passed" a stage on code
#      from before the change -- the rule is written in the sanitizer script)
#   1. t/ on objs             the whole functional suite, interpreter build
#   2. t/comcon_*.t on objs_jit   the confinement corpus, compiled tier
#   2b. t/tools/jit-diff-fuzz.py  the compiled tier fuzzed against the interpreter
#      (quickjs/qjs, every function compiled; twelve seeds; the instrument
#      that found F20 and F21, which no sanitizer and no shape the corpus
#      held could see)
#   3. t_stress/ on objs      the leak and reload stress suites
#   4. t/run_sanitizers.sh    ASAN + UBSAN over the corpus, leaks ON
#
# NEVER `prove -j`: the suites own fixed ports and race under parallelism.
# Never two suites at once for the same reason -- this script is sequential.
#
# WHAT IT ASSUMES: libquickjs.a is the CONFIG_JIT build (both objs and
# objs_jit link the same archive; objs is the interpreter only because it
# lacks the server-AOT call), and the four builddirs are configured.  It
# checks both and says what is missing rather than running on the wrong
# thing.  The reviewer pack (t/tools/reviewer-pack.sh) is the SUPERSET a
# signer runs: it adds the three doc checkers and every negative control.
#
# The logs land in $GATE_OUT (default /tmp/gate-<timestamp>); the summary is
# printed at the end and the exit code is the verdict.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2

MODULES="--with-http_ssl_module --with-http_v2_module --with-http_v3_module \
--with-http_realip_module --with-http_addition_module --with-http_sub_module \
--with-http_gunzip_module --with-http_gzip_static_module --with-http_auth_request_module \
--with-http_random_index_module --with-http_secure_link_module --with-http_degradation_module \
--with-http_slice_module --with-http_stub_status_module --with-stream \
--with-http_image_filter_module --with-http_xslt_module --with-http_mp4_module"

if [ "${1:-}" = "--configure" ]; then
    # One archive for every build: the JIT one.  `make -C quickjs clean` first,
    # because make does not track the CONFIG_JIT flag change (a stale quickjs.o
    # against a fresh quickjs-jit.o is an ABI mismatch that links and corrupts).
    make -C quickjs clean >/dev/null && make -C quickjs CONFIG_JIT=y libquickjs.a || exit 1
    auto/configure --add-module=src/js \
        --with-cc-opt='-Iquickjs -Wno-cast-function-type' \
        --with-ld-opt='-Lquickjs -lquickjs -lm' $MODULES || exit 1
    auto/configure --builddir=objs_jit --add-module=src/js \
        --with-cc-opt='-Iquickjs -Wno-cast-function-type -DCONFIG_JIT' \
        --with-ld-opt='-Lquickjs -lquickjs -lm -ldl -lpthread' $MODULES || exit 1
    auto/configure --builddir=objs_asan --add-module=src/js \
        --with-cc-opt='-Iquickjs -Wno-cast-function-type -fsanitize=address -fno-omit-frame-pointer -g -O0' \
        --with-ld-opt='-Lquickjs -lquickjs -lm -fsanitize=address' $MODULES || exit 1
    auto/configure --builddir=objs_ubsan --add-module=src/js \
        --with-cc-opt='-Iquickjs -Wno-cast-function-type -fsanitize=undefined -fno-omit-frame-pointer -g -O0' \
        --with-ld-opt='-Lquickjs -lquickjs -lm -fsanitize=undefined' $MODULES || exit 1
    # auto/configure leaves the root Makefile pointing at the LAST builddir:
    # never run a bare `make` here; build with `make -f <builddir>/Makefile build`.
    for d in objs objs_jit objs_asan objs_ubsan; do
        make -f $d/Makefile build -j"$(nproc)" >/dev/null || exit 1
    done
    echo "configured and built: objs objs_jit objs_asan objs_ubsan"
    exit 0
fi

# (grep -c, not grep -q: under pipefail an early-closing grep makes nm's SIGPIPE the verdict)
if [ "$(nm quickjs/libquickjs.a 2>/dev/null | grep -c ' T js_jit_close_caps')" = 0 ]; then
    echo "REFUSING: quickjs/libquickjs.a is not the CONFIG_JIT build (run --configure)"
    exit 2
fi
for d in objs objs_jit objs_asan objs_ubsan; do
    [ -f "$d/Makefile" ] || { echo "REFUSING: $d is not configured (run --configure)"; exit 2; }
done

OUT=${GATE_OUT:-/tmp/gate-$(date -u +%Y%m%dT%H%M%SZ)}
mkdir -p "$OUT"
fail=0
verdict=()

stage() {  # name, logfile, command...
    local name="$1" log="$2"; shift 2
    echo "=== $name ==="
    "$@" >"$OUT/$log" 2>&1
    local rc=$?
    grep -E "^Files=|^Result|Failed test|^S6 sanitizer gate|findings in src/js|^jit-diff-fuzz:|^seed [0-9]+: [0-9]+ diverging|^DEAD" "$OUT/$log" | head -12
    if [ $rc -eq 0 ]; then verdict+=("PASS  $name"); else verdict+=("FAIL  $name  (see $OUT/$log)"); fail=1; fi
}

# G-21 (v5.131): the artifact cache is THIS run's.  The master's helper writes
# an index per live epoch; a previous run's index would answer at include time
# and turn stage 2 into a warm pass by accident (which is how F22 was found,
# and why the warm pass is now a stage of its own, 2c).
export QJS_JIT_CACHE="$OUT/jitcache"
mkdir -p "$QJS_JIT_CACHE"

echo "=== 0. rebuild the sanitizer builddirs ==="
for d in objs_asan objs_ubsan; do
    rm -f "$d/nginx"
    make -f "$d/Makefile" build -j"$(nproc)" >"$OUT/build-$d.log" 2>&1 || { echo "FAIL  rebuild $d"; exit 1; }
done
for d in objs objs_jit; do
    make -f "$d/Makefile" build -j"$(nproc)" >"$OUT/build-$d.log" 2>&1 || { echo "FAIL  rebuild $d"; exit 1; }
done
# the JIT shell for stage 2b (same archive, same flags; a no-op when current)
make -C quickjs CONFIG_JIT=y qjs >"$OUT/build-qjs.log" 2>&1 || { echo "FAIL  rebuild quickjs/qjs"; exit 1; }

stage "1. t/ on objs"               t-objs.log     env TEST_NGINX_BINARY="$PWD/objs/nginx"     prove t/
stage "2. t/comcon_*.t on objs_jit" t-jit.log      env TEST_NGINX_BINARY="$PWD/objs_jit/nginx" prove t/comcon_*.t
# G-21 (v5.131): the second pass is the WARM one.  Pass 2 asked the master to
# compile every request-time fragment; the index it left answers at include
# time now, so every fragment the suite admits at request time runs NATIVE
# inside the request that admits it.  This is the pass that found F22 (a
# frozen global reassigned by compiled code): the compiled tier's confinement
# was only ever exercised by config-time fragments before it.
stage "2c. t/comcon_*.t on objs_jit, warm artifact cache" t-jit-warm.log env TEST_NGINX_BINARY="$PWD/objs_jit/nginx" prove t/comcon_*.t
stage "2b. jit-diff-fuzz (12 seeds)" jit-fuzz.log   env JIT_FUZZ_TMP="$OUT/jit-fuzz" python3 t/tools/jit-diff-fuzz.py run --seeds 1-12 --n 60
stage "3. t_stress/ on objs"        t-stress.log   env TEST_NGINX_BINARY="$PWD/objs/nginx"     prove t_stress/
stage "4. sanitizers (leaks on)"    sanitizers.log bash t/run_sanitizers.sh

echo
echo "================================================================"
printf '%s\n' "${verdict[@]}"
echo "logs: $OUT"
[ $fail -eq 0 ] && echo "GATE GREEN" || echo "GATE FAILED"
exit $fail
