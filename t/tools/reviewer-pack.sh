#!/bin/bash
#
# THE REVIEWER PACK — what a second signer runs.
#
# F11 is the last finding in the assurance ledger that no amount of engineering
# closes: `AUDIT_M-SES.md` §5 and `ASSURANCE.md` §15 each carry ONE signer, and
# both signature rows say the same thing in their own words -- "the §4 commands
# were executed by the authoring session, not independently re-run by the
# signer", so what exists is acceptance of reproducible evidence, not a
# reproduction.  A2 accepts that as residual risk.
#
# The gap is not missing evidence.  It is that reproducing it currently means
# reading two documents totalling ~1100 lines, extracting the commands by hand,
# knowing which builddirs are stale, and judging which numbers matter.  This
# script is that work, done once, so a reviewer's job becomes: run one command,
# read a verdict table, decide whether to sign.
#
# TWO CLASSES OF RESULT, and the separation is deliberate:
#
#   GATE      objectively pass/fail, and this script's exit code depends on it.
#             A suite passes or it does not; a sanitizer reports findings in
#             src/js or it does not; a checker exits 0 or it does not; the
#             negative controls hold or they do not.
#   REPORTED  numbers a reviewer should LOOK at and this script must not judge:
#             file and test counts, timings, coverage denominators.  They drift
#             legitimately as tests are added, so gating on them would make the
#             pack fail for the wrong reason -- and pinning them here would put
#             a second copy of every count outside the documents that own them.
#
# WHAT THIS SCRIPT DELIBERATELY DOES NOT DO.  It does not tell you whether to
# sign, it does not summarise the residuals you would be accepting, and it does
# not reproduce the findings ledger.  Those live in `ASSURANCE.md` §15/§16 and
# `AUDIT_M-SES.md` §3/§5/§6, and a signer has to read them there -- a script that
# restated them would become a second place for them to be wrong, and a
# signature on a summary is worth less than no signature.  `REVIEW.md` is the
# procedure and the sign-off block; this is the evidence runner.
#
# Usage:
#     bash t/tools/reviewer-pack.sh            # everything (~40 min)
#     bash t/tools/reviewer-pack.sh --quick    # skip sanitizers + controls (~8 min)
#
# It writes a transcript you can attach to a signature.

set -u
cd "$(dirname "$0")/../.." || exit 2

QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1

TS=$(date -u +%Y%m%dT%H%M%SZ)
# OUTSIDE the repository, always.  This script refuses to run on a dirty tree,
# so it must not be the thing that dirties it -- writing a transcript and four
# build logs into the working copy would make the second invocation refuse
# because of the first.  Override with REVIEWER_PACK_OUT=/some/dir.
OUT="${REVIEWER_PACK_OUT:-${TMPDIR:-/tmp}/reviewer-pack-$TS}"
mkdir -p "$OUT" || exit 2
LOG="$OUT/transcript.txt"
# G-21 (v5.131): this run's own artifact cache (see t/tools/gate.sh)
export QJS_JIT_CACHE="$OUT/jitcache"
mkdir -p "$QJS_JIT_CACHE"

gate_fail=0
declare -a GATE_ROWS
declare -a REPORT_ROWS

say  () { printf '%s\n' "$*" | tee -a "$LOG"; }
head2() { say ""; say "── $* ──"; }

gate () {  # gate <label> <0|1> [detail]
    local label="$1" rc="$2" detail="${3:-}"
    if [ "$rc" = 0 ]; then
        GATE_ROWS+=("PASS|$label|$detail")
        say "  PASS  $label"
    else
        GATE_ROWS+=("FAIL|$label|$detail")
        gate_fail=1
        say "  FAIL  $label   $detail"
    fi
}
report () { REPORT_ROWS+=("$1|$2"); say "  ....  $1: $2"; }

say "REVIEWER PACK — $TS"
say "output dir: $OUT   (outside the repo, so this run cannot dirty the tree)"

# ───────────────────────────────────────────────────────── provenance ────
head2 "0. Provenance (what you are about to attest to)"
say "  commit      : $(git rev-parse HEAD 2>/dev/null || echo '(not a git tree)')"
say "  branch      : $(git rev-parse --abbrev-ref HEAD 2>/dev/null)"
say "  gcc         : $(gcc --version 2>/dev/null | head -1)"
say "  perl        : $(perl -e 'print $^V' 2>/dev/null)"
say "  host        : $(uname -srm)"

dirty=$(git status --porcelain src/js quickjs t js_comcon 2>/dev/null | wc -l)
if [ "$dirty" != 0 ]; then
    say ""
    say "  REFUSING: the tree has $dirty uncommitted change(s) under"
    say "            src/js, quickjs, t or js_comcon.  A signature has to name a"
    say "            commit; evidence gathered from a modified tree names nothing."
    git status --porcelain src/js quickjs t js_comcon | sed 's/^/            /' | tee -a "$LOG"
    exit 2
fi
say "  tree        : clean under src/js, quickjs, t, js_comcon"

# ─────────────────────────────────────────────────────────── rebuild ─────
head2 "1. Rebuild every builddir (§4: the committed binaries are STALE ARTIFACTS)"
say "  The objs*/ trees are tracked in git, so a fresh clone or a reset --hard"
say "  gives you a committed binary rather than one built from the source under"
say "  audit.  Assembling the audit, that produced a FAIL on objs_jit that the"
say "  real build passes.  Rebuilding is not optional."
built=0
for d in objs objs_jit objs_asan objs_ubsan; do
    [ -d "$d" ] || continue
    if make -f "$d/Makefile" build >"$OUT/build_$d.log" 2>&1; then
        say "  built $d"
        built=$((built+1))
    else
        say "  BUILD FAILED: $d (see $OUT/build_$d.log)"
        gate "rebuild $d" 1 "see $OUT/build_$d.log"
    fi
done
gate "all builddirs rebuilt from the audited source" "$([ "$built" -ge 2 ] && echo 0 || echo 1)" \
     "$built builddir(s)"
report "builddirs rebuilt" "$built"

# Freshness, checked rather than assumed: the binary must be newer than the
# newest source it is built from.  This is the mechanical form of §4's warning.
#
# GENERATED C IS NOT SOURCE.  `quickjs/repl.c` is qjsc output (Makefile line
# "repl.c: $(QJSC) repl.js"), gitignored, and regenerated by `make -C quickjs
# test` -- and no nginx binary is built from it.  Globbing every *.c under
# quickjs/ therefore reported both binaries STALE after a clean engine test run,
# which is a gate failing for a reason that has nothing to do with the tree under
# audit.  The set is the files the nginx build actually compiles.
newest_src=$(find src/js quickjs -name '*.c' -o -name '*.h' 2>/dev/null \
             | grep -vE '/(repl|qjscalc|out)\.c$' \
             | xargs ls -t 2>/dev/null | head -1)
stale=0
for d in objs objs_jit; do
    [ -x "$d/nginx" ] || continue
    [ -n "$newest_src" ] && [ "$newest_src" -nt "$d/nginx" ] && { stale=1; say "  STALE: $d/nginx older than $newest_src"; }
done
gate "binaries are newer than the newest source file" "$stale"

# ──────────────────────────────────────────────────────────── suites ─────
head2 "2. The suites (§4)"
for d in objs objs_jit; do
    [ -x "$d/nginx" ] || continue
    TEST_NGINX_BINARY="$PWD/$d/nginx" prove t/ >"$OUT/prove_$d.log" 2>&1
    rc=$?
    n=$(grep -oE 'Files=[0-9]+, Tests=[0-9]+' "$OUT/prove_$d.log" | head -1)
    gate "t/ on $d" "$rc" "$n"
    report "t/ on $d" "${n:-no summary line}"
    [ "$rc" = 0 ] || grep -E "^t/\S+ +\(Wstat|Failed tests?:" "$OUT/prove_$d.log" | head -6 | sed 's/^/        /' | tee -a "$LOG"
done
# G-21 (v5.131): the WARM pass -- every request-time fragment admitted native
# (the index the first objs_jit pass left answers at include time); the pass
# that found F22.  See t/tools/gate.sh stage 2c.
if [ -x objs_jit/nginx ]; then
    TEST_NGINX_BINARY="$PWD/objs_jit/nginx" prove t/comcon_*.t >"$OUT/prove_objs_jit_warm.log" 2>&1
    rc=$?
    n=$(grep -oE 'Files=[0-9]+, Tests=[0-9]+' "$OUT/prove_objs_jit_warm.log" | head -1)
    gate "t/comcon_*.t on objs_jit, warm artifact cache" "$rc" "$n"
    report "t/comcon_*.t on objs_jit (warm)" "${n:-no summary line}"
    [ "$rc" = 0 ] || grep -E "^t/\S+ +\(Wstat|Failed tests?:" "$OUT/prove_objs_jit_warm.log" | head -6 | sed 's/^/        /' | tee -a "$LOG"
fi
TEST_NGINX_BINARY="$PWD/objs/nginx" prove t_stress/ >"$OUT/prove_stress.log" 2>&1
rc=$?
gate "t_stress/ on objs" "$rc" "$(grep -oE 'Files=[0-9]+, Tests=[0-9]+' "$OUT/prove_stress.log" | head -1)"

# ────────────────────────────────────────────────────── standing checks ──
head2 "3. The standing checkers (drift is a build failure here)"
for c in check-assurance.py check-enumerations.py check-dead-probes.py; do
    python3 "t/tools/$c" >"$OUT/$c.log" 2>&1
    gate "$c" $? "$(tail -1 "$OUT/$c.log")"
done
# The compiled tier against the interpreter (G7.23): random numeric functions,
# every one compiled, the interpreter as oracle.  Needs the JIT shell.
make -C quickjs CONFIG_JIT=y qjs >"$OUT/build-qjs.log" 2>&1
JIT_FUZZ_TMP="$OUT/jit-fuzz" python3 t/tools/jit-diff-fuzz.py run --seeds 1-12 --n 60 >"$OUT/jit-diff-fuzz.log" 2>&1
gate "jit-diff-fuzz.py, 12 seeds x 60 functions, compiled == interpreted" $? "$(tail -1 "$OUT/jit-diff-fuzz.log")"

# ───────────────────────────────────────────────────────── the slow half ─
if [ "$QUICK" = 1 ]; then
    head2 "4/5. SKIPPED (--quick)"
    say "  The sanitizers and the negative controls were NOT run.  A signature"
    say "  based on a --quick run attests less than one based on a full run, and"
    say "  the sign-off block in REVIEW.md has a line for saying which you did."
    REPORT_ROWS+=("sanitizers|NOT RUN (--quick)")
    REPORT_ROWS+=("negative controls|NOT RUN (--quick)")
else
    head2 "4. Memory safety (§4: builds its own guards in; refuses a vacuous run)"
    bash t/run_sanitizers.sh >"$OUT/sanitizers.log" 2>&1
    rc=$?
    gate "ASAN+UBSAN over the COMCON corpus, 0 findings in src/js" "$rc" \
         "$(grep -E 'S6 sanitizer gate' "$OUT/sanitizers.log" | tail -1)"
    # Shown, not just logged.  The first version piped these through
    # `tee -a "$LOG" >/dev/null`, which put them in the transcript and threw
    # them away on screen -- so a reviewer saw a section heading with nothing
    # under it, which is worse than printing no heading at all.
    grep -E 'findings in src/js|findings elsewhere|tests run' "$OUT/sanitizers.log" \
        | sed 's/^/        /' | tee -a "$LOG"

    head2 "5. Negative controls (do the fixes' tests actually fail without the fix?)"
    # a row that does not hold keeps its prove -v output and the test's own
    # directory under the pack's output, so a flake leaves evidence, not a verdict
    VNC_EVIDENCE="$OUT/controls-evidence" \
        bash t/tools/verify-negative-controls.sh >"$OUT/controls.log" 2>&1
    rc=$?
    gate "every automated negative control holds" "$rc" \
         "$(grep -E '^verified [0-9]+' "$OUT/controls.log" | tail -1)"
    report "negative controls" "$(grep -E '^verified [0-9]+' "$OUT/controls.log" | tail -1)"
    # Since v5.120 every control is automated: a commit revert or a MAINTAINED
    # reverse patch under t/tools/controls/.  A row whose patch no longer
    # applies is INCONCLUSIVE and FAILS the gate above -- the patch has to be
    # re-based, not listed.  Print any such row so the reader sees which.
    if grep -q INCONCLUSIVE "$OUT/controls.log"; then
        say "  INCONCLUSIVE rows (a patch that no longer applies, or a test that did not run):"
        grep -B2 INCONCLUSIVE "$OUT/controls.log" | grep -E '^===|INCONCLUSIVE' | sed 's/^/        /' | tee -a "$LOG"
    fi
fi

# ─────────────────────────────────────────────── reported measurements ───
head2 "6. Reported, not gated (look at these; this script does not judge them)"
report "assurance leaves"   "$(grep -cE '^#### G[0-9]+\.' js_comcon/docs-v5.0/ASSURANCE.md)"
report "findings in ledger" "$(grep -cE '^\| \*\*F[0-9]+\*\* \|' js_comcon/docs-v5.0/ASSURANCE.md)"
report "t/ files"           "$(ls t/*.t | wc -l)"
report "comcon_*.t files"   "$(ls t/comcon_*.t | wc -l)"
# The NEWEST entry, by version number.  `head -1` gave v5.1 -- the delta log is
# not strictly ordered in the file, and the first match is an early entry in the
# body rather than the current one.  A provenance line that is quietly wrong is
# the one thing this script must not produce.
report "delta-log version"  "$(grep -oE '^\*\*v5\.[0-9]+' js_comcon/docs-v5.0/FOUNDATION.md \
                                 | sort -t. -k2 -n | tail -1)"

# ───────────────────────────────────────────────────────────── verdict ───
head2 "VERDICT"
for row in "${GATE_ROWS[@]}"; do
    printf '  %-5s %s\n' "${row%%|*}" "$(echo "$row" | cut -d'|' -f2)" | tee -a "$LOG"
done
say ""
for row in "${REPORT_ROWS[@]}"; do
    printf '  %-28s %s\n' "${row%%|*}" "${row#*|}" | tee -a "$LOG"
done
say ""
if [ "$gate_fail" = 0 ]; then
    say "ALL GATES PASS on $(git rev-parse --short HEAD)."
    say ""
    say "That is the EVIDENCE half and it is the smaller half.  Before signing,"
    say "read js_comcon/docs-v5.0/REVIEW.md -- signing attests acceptance of the"
    say "residuals named in ASSURANCE.md §15/§16 and AUDIT_M-SES.md §3, and a"
    say "signature applied without reading those converts \"we know these holes"
    say "exist\" into \"someone looked and found nothing\"."
    say ""
    say "Transcript to attach: $LOG"
    exit 0
fi
say "ONE OR MORE GATES FAILED — do not sign.  See the FAIL rows above and the"
say "logs in $OUT/.  A gate failure is either a real regression or a stale"
say "builddir; §4 explains why the second is the likelier of the two."
exit 1
