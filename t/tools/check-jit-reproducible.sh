#!/bin/bash
#
# V14 — same fragment, same toolchain => bit-identical .so.
#
# The compile->sign->cache story assumes this and nothing checked it.  Without
# it a signature over an artifact attests WHICH COMPILE produced it rather than
# WHAT IS IN IT: two honest compiles of one fragment disagree, so signature
# equality cannot be used to decide that a cached .so matches a fragment.
#
# WHAT THIS FOUND (2026-09-13).  The claim was FALSE, in exactly six bytes.  The
# generated C was byte-identical across runs; GCC records the translation unit's
# filename as an STT_FILE symbol, and that name came from mkstemps --
# `qjs_jit_2m6d6L.c` one run, `qjs_jit_Y1siAd.c` the next.  Fixed by
# `jit_write_repro()`: a private directory per job, a basename derived from the
# bytecode hash, and GCC invoked with its cwd set there so the bare name is what
# reaches the symbol table.  The directory stays unique on purpose -- a
# deterministic PATH would put two processes on one file.
#
# THIS IS A HAND-RUN INSTRUMENT, like run_sanitizers.sh and
# verify-negative-controls.sh: it needs a JIT-capable qjs, which the normal build
# does not produce.  Evidence, not a gate.
#
#   bash t/tools/check-jit-reproducible.sh
#
# It builds qjs with CONFIG_JIT if that binary is missing or older than the
# engine source -- a stale qjs reports the behaviour of the code it was built
# from, which is indistinguishable from a fix that works.  That trap cost real
# time here: the first run of this probe reported `--jit-aot` as an unknown
# option because qjs.o was reused from a non-JIT build.

set -u
cd "$(dirname "$0")/../.." || exit 2

say () { printf '%s\n' "$*"; }

TMPDIR_LOG=$(mktemp /tmp/v14_build_XXXXXX.log) || exit 2
trap 'rm -f "$TMPDIR_LOG"' EXIT

if ! command -v gcc >/dev/null 2>&1; then
    say "SKIP: no gcc -- the JIT compiles through it, so there is nothing to test"
    exit 0
fi

# Build qjs if missing or older than the engine it is built from.
if [ ! -x quickjs/qjs ] || [ quickjs/quickjs-jit.c -nt quickjs/qjs ] \
   || [ quickjs/quickjs.c -nt quickjs/qjs ]; then
    say "building a JIT-capable qjs (missing or older than the engine source)"
    if ! make -C quickjs CONFIG_JIT=y qjs >"$TMPDIR_LOG" 2>&1; then
        # REFUSING, not SKIP.  The first version of this script exited 0 here,
        # and when a real compile error was introduced it reported
        # "could not build ... SKIP" -- a broken engine reading as "nothing to
        # test".  A skip is for a toolchain that is absent, never for one that
        # is present and failing.
        say "REFUSING: quickjs/qjs failed to BUILD with CONFIG_JIT=y -- that is"
        say "          a broken tree, not an absent toolchain:"
        grep -E "error|Error" "$TMPDIR_LOG" | head -5 | sed 's/^/    /'
        exit 2
    fi
fi

# The option must exist, or the run below would measure an interpreter.
if ! ./quickjs/qjs --help 2>&1 | grep -q -- "--jit-aot"; then
    say "REFUSING: this qjs does not advertise --jit-aot, so it was built"
    say "          without CONFIG_JIT and would compile nothing.  A clean"
    say "          result from it would mean nothing at all."
    exit 2
fi

work=$(mktemp -d /tmp/v14_repro_XXXXXX) || exit 2
trap 'rm -rf "$work"' EXIT

cat > "$work/frag.js" <<'JS'
/* Compute-bearing, so there is something for GCC to lower. */
function hot(n) {
    var s = 0, i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < 12; j++) { s = (s + i * j) | 0; }
        if ((s & 1023) === 7) { s = (s ^ 0x5a5a) | 0; }
    }
    return s;
}
var r = 0;
for (var k = 0; k < 400; k++) { r = (r + hot(120)) | 0; }
print("r=" + r);
JS

mkdir -p "$work/c1" "$work/c2"
for c in c1 c2; do
    QJS_JIT_CACHE="$work/$c" ./quickjs/qjs --jit-aot "$work/frag.js" \
        > "$work/$c.out" 2>&1
done

# The two runs must have COMPUTED the same thing, or a byte comparison of their
# artifacts is comparing two different programs.
if ! cmp -s "$work/c1.out" "$work/c2.out"; then
    say "REFUSING: the two runs printed different output, so they are not two"
    say "          compiles of one fragment:"
    sed 's/^/    c1: /' "$work/c1.out" | head -3
    sed 's/^/    c2: /' "$work/c2.out" | head -3
    exit 2
fi

so1=$(ls "$work"/c1/*.so 2>/dev/null | head -1)
so2=$(ls "$work"/c2/*.so 2>/dev/null | head -1)

# No artifact means nothing was compiled -- the failure mode that looks like a
# pass if you only diff what exists.
if [ -z "$so1" ] || [ -z "$so2" ]; then
    say "REFUSING: no .so was produced, so nothing was compiled and there is"
    say "          nothing to compare.  Artifacts found:"
    say "            c1: $(ls "$work"/c1 2>/dev/null | tr '\n' ' ')"
    say "            c2: $(ls "$work"/c2 2>/dev/null | tr '\n' ' ')"
    exit 2
fi

say "fragment output : $(head -1 "$work/c1.out")"
say "cache key       : $(basename "$so1" .so)"
say "artifact size   : $(stat -c %s "$so1") bytes"

if cmp -s "$so1" "$so2"; then
    say ""
    say "V14: two cold compiles of the same fragment are BIT-IDENTICAL"
    say "     sha256 $(sha256sum "$so1" | cut -d' ' -f1)"
    exit 0
fi

say ""
say "V14 FAILS: the same fragment compiled to DIFFERENT bytes"
say "  differing bytes: $(cmp -l "$so1" "$so2" | wc -l) of $(stat -c %s "$so1")"
say "  recorded translation-unit names (the usual culprit):"
for f in "$so1" "$so2"; do
    say "    $(readelf -s "$f" 2>/dev/null | awk '$4=="FILE"{printf "%s ", $NF}')"
done
say "  first differing offsets:"
cmp -l "$so1" "$so2" | head -8 | sed 's/^/    /'
exit 1
