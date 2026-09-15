#!/usr/bin/env bash
#
# Verify the negative controls behind AUDIT_M-SES.md §2b.
#
# Every §2b row claims: revert this fix and the named test fails.  That is the
# claim which makes the row checkable by someone who did not write it, and this
# script is how you check it without taking anyone's word for the procedure.
#
# For each row it reverts the fix, rebuilds, runs the named test, and requires
# it to FAIL.  Then it restores, rebuilds, and requires it to PASS again.  A row
# passes only if the test fails without the fix and passes with it.
#
#   bash t/tools/verify-negative-controls.sh            # every row
#   bash t/tools/verify-negative-controls.sh 67bc359e9  # one row, by id
#
# TWO KINDS OF ROW, one mechanism each:
#
#   ROWS     -- the fix is a commit whose src/js half still reverts cleanly:
#               `git show <sha> -- src/js | git apply -R`.
#   PATCHES  -- the fix's lines were rewritten since, or the fix lives in
#               quickjs/, or the control is not a whole commit but the smallest
#               change that brings the defect back.  For these the control is a
#               MAINTAINED reverse patch under t/tools/controls/, applied with
#               `git apply` (never `-3`: see below).  The patch is the row's
#               claim made mechanical -- "drop these lines and this test fails"
#               -- and it is checked here like any other row.  When a later
#               commit rewrites the lines a patch targets, `git apply` refuses,
#               the row reports INCONCLUSIVE, and the pack's gate FAILS: the
#               patch has to be re-based, on purpose, by whoever moved the lines.
#               That is the whole point of keeping them: the debt cannot
#               accumulate silently again (twenty rows had, by v5.117).
#
# A patch may touch quickjs/ (the vendored engine); the script then rebuilds
# libquickjs.a and forces the relink the nginx Makefile does not track.  A row's
# expectation is `fail` (the test must fail) or `leak:<symbol>` (run under
# objs_asan with leak detection on, and a leak report naming <symbol> must
# appear -- these rows' defects are leaks, not failing assertions).
#
# WHY IT REBUILDS EVERY TIME.  The objs*/ directories are tracked in git, so a
# checkout hands you a committed binary rather than one built from the source in
# front of you.  Testing a stale binary has already produced a wrong answer
# twice during this work -- once reporting a gate FAILING that passes, once
# reporting a control PASSING that had never run.  The rebuild is the point, not
# the overhead.
#
# DO NOT REACH FOR `git apply -R -3`.  A three-way apply looks like the obvious
# way to absorb context drift, and it is worse than failing: tried on these two
# rows it applied one file cleanly, failed the other with "does not match index",
# and LEFT THE PARTIAL REVERT IN THE TREE.  A control that half-reverts a fix and
# walks away is how a tree quietly stops being the tree you tested.  When a
# patch no longer applies, the row is INCONCLUSIVE and the run fails; re-base
# the patch.
#
# WHAT IT WILL NOT DO.  It refuses to start on a dirty tree (src/js, quickjs),
# reverts nothing outside src/js and quickjs, and restores the tree on any exit
# including Ctrl-C.  If it ever leaves the tree modified,
# `git checkout -- src/js quickjs` is the whole recovery.
#
# A SKIPPED TEST IS NOT A PASS.  A two-binary test that cannot find its other
# build skips itself, and `prove` reports that as a pass; here it is
# INCONCLUSIVE, because a control that did not run proves nothing either way.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2

ONLY=${1:-}

# sha | test | build | label
ROWS=(
"6f4b0edb6|t/comcon_declarative_fuzz.t|objs|declarative review: a CR hid live code"
"1358dd0ff|t/js_com_setter_fuzz.t|objs|describe() misdeclared four member types"
"d3a438051|t/js_com_broadcast_fuzz.t|objs_ubsan|broadcast: misaligned header read"
"e246cc347|t/js_com_numeric_range.t|objs|numbers cast instead of checked"
"a321849fa|t/js_com_lb_select.t|objs|balancer with no return pinned peer 0"
"335dc0956|t/js_com_filter_nongenerator.t|objs|filter with no return dropped the response"
"c5bf0ceac|t/js_pilgrim_p17_l4_window.t|objs|the L4 window armed no timer, so it was a wait state with no deadline"
"60e6d5585|t/comcon_oom_sweep.t|objs+objs_jit|F19: the host's ToString of a fragment's error ran out of memory itself, reported as error with its exception left pending"
)

# id | patch (under t/tools/controls/) | test | build (objs, objs_jit, objs_asan, objs_ubsan, or objs+objs_jit for a two-binary test) | expect (fail, leak:<symbol>) | label
PATCHES=(
"cd391a160|socket-handle-generation.patch|t/js_com_socket_fuzz.t|objs|fail|a closed handle could close someone else's socket: the handle's generation check is gone"
"07c7fd277|listener-generation.patch|t/js_com_socket_fuzz.t|objs|fail|a retired listener still pointed at its old slot: the listener's generation check is gone"
"5186565a1|num-range-unchecked.patch|t/js_com_peer_range.t|objs|fail|peer numbers cast, not checked: ngx_js_com_num_range() refuses nothing"
"0ebac7e47|num-range-unchecked.patch|t/js_com_ssl_range.t|objs|fail|SSL numbers cast, not checked: the same helper"
"21b42d7e5|grant-declare-confers-nothing.patch|t/js_com_grant_declare.t|objs|fail|grantToTenant reported a capability it did not confer"
"67bc359e9|admission-by-truthiness.patch|t/comcon_include_contract_fuzz.t|objs|fail|a malformed contract silently disabled admission"
"d5c880cd4|leftovers-charged-to-caller.patch|t/comcon_leftover_accounting.t|objs|fail|a fragment's leftovers were charged to the next caller"
"ffd76ed84|invoke-walks-the-heap.patch|t/comcon_invoke_heap_independence.t|objs|fail|an invocation cost what the compartment held"
"ffd76ed84b|oom-read-as-null.patch|t/comcon_fragment_error_report.t|objs|fail|out of memory reported as null"
"0317e8b90|globals-not-frozen.patch|t/comcon_global_binding_freeze.t|objs|fail|a fragment could reassign a shared global"
"cabd6f4c2|interrupt-needs-worker.patch|t/comcon_deadline_without_worker.t|objs|fail|no interrupt handler before a worker existed"
"1618dce79|wrapper-not-checked.patch|t/comcon_wrapper_breakout.t|objs|fail|a source could escape the wrapper it was compiled inside"
"b75e7d9e5|bounds-not-a-stack.patch|t/comcon_author_basic.t|objs|fail|nested bounds did not meet the enclosing ones"
"8c57ded4b|author-include-refused.patch|t/comcon_author_basic.t|objs|fail|the authoring tier: author.include() refuses everything"
"37b3c2057|regrant-allow-unchecked.patch|t/comcon_author_regrant.t|objs|fail|re-granting: allow no longer asserted a subset of the parent's mask"
"copy-vs-rewrap|regrant-rewraps.patch|t/comcon_author_regrant.t|objs|fail|a re-grant re-wrapped the handle instead of copying the opaque"
"nested-marshal|nested-marshal-object.patch|t/comcon_author_basic.t|objs|fail|the nested marshal let an object cross"
"F16|jit-catch-ignores-uncatchable.patch|t/comcon_jit_uncatchable.t|objs_jit|fail|compiled code could catch its own deadline"
"F17a|teardown-skipped.patch|t/comcon_v12_denial_codes.t|objs_asan|leak:ngx_js_comcon_publish|a worker never freed the compartment at exit"
"F17b|com-classes-unregistered.patch|t/comcon_v12_denial_codes.t|objs_asan|leak:ngx_js_wrap_server|COM node classes finalizer-less in the compartment"
"F18|backtrace-frees-the-error.patch|t/comcon_oom_backtrace.t|objs|fail|an out-of-memory inside the backtrace annotation freed the error"
"M5.1a|half-typed-bitops-wrong.patch|t/comcon_include_faithfulness.t|objs+objs_jit|fail|M5.1a: xor emitted as or, Int8 read as Uint8"
)

if ! git diff --quiet -- src/js quickjs || ! git diff --cached --quiet -- src/js quickjs; then
    echo "REFUSING: src/js or quickjs has uncommitted changes."
    echo "This script reverts and restores them; it will not run over your work."
    exit 2
fi

restore() {
    git checkout -- src/js quickjs 2>/dev/null
}
trap restore EXIT INT TERM

# rebuild $1=builddir [$2=1 if the engine changed]
rebuild() {
    local d="$1" engine="${2:-0}" b
    if [ "$engine" = 1 ]; then
        make -C quickjs CONFIG_JIT=y libquickjs.a >/tmp/vnc-build.log 2>&1 || return 1
    fi
    for b in ${d//+/ }; do
        # the nginx Makefile does not track libquickjs.a: force the relink
        [ "$engine" = 1 ] && rm -f "$b/nginx"
        make -f "$b/Makefile" build -j"$(nproc)" >>/tmp/vnc-build.log 2>&1 || return 1
    done
    return 0
}

# run $1=test $2=builddir $3=expect ; echo PASS, FAIL or SKIP
run_test() {
    local t="$1" d="$2" expect="${3:-fail}" out first
    first=${d%%+*}
    case "$expect" in
    leak:*)
        # the defect is a LEAK: a report naming the symbol must appear
        rm -f /tmp/vnc-asan.*
        LSAN_OPTIONS="suppressions=$PWD/t/tools/lsan.supp" \
        ASAN_OPTIONS="detect_leaks=1:log_path=/tmp/vnc-asan" \
            TEST_NGINX_BINARY="$PWD/$first/nginx" prove "$t" >/dev/null 2>&1
        if cat /tmp/vnc-asan.* 2>/dev/null | grep -q "${expect#leak:}"; then
            echo FAIL       # the named leak is present == the defect is present
        else
            echo PASS
        fi
        return
        ;;
    esac
    if [ "$first" = objs_ubsan ]; then
        # this row's control is a SANITIZER finding, not a failing assertion
        rm -f /tmp/vnc-ubsan.*
        UBSAN_OPTIONS="log_path=/tmp/vnc-ubsan:halt_on_error=0" \
            TEST_NGINX_BINARY="$PWD/$first/nginx" prove "$t" >/dev/null 2>&1
        if cat /tmp/vnc-ubsan.* 2>/dev/null | grep -q "src/js"; then
            echo FAIL          # a finding in src/js == the defect is present
        else
            echo PASS
        fi
        return
    fi
    out=$(TEST_NGINX_BINARY="$PWD/$first/nginx" prove "$t" 2>&1)
    if echo "$out" | grep -qi "skipped:"; then echo SKIP; return; fi
    if echo "$out" | grep -q "^Result: PASS"; then echo PASS; else echo FAIL; fi
}

pass=0; fail=0; skipped=0

# one control: $1=id $2=label $3=test $4=build $5=expect $6=apply-cmd $7=engine
check_row() {
    local id="$1" label="$2" test="$3" dir="$4" expect="$5" apply="$6" engine="$7" got b

    echo
    echo "=== $id  $label"
    echo "    test: $test   build: $dir   expect: $expect"

    for b in ${dir//+/ }; do
        if [ ! -x "$b/nginx" ]; then
            echo "    SKIP: $b is not configured here"
            skipped=$((skipped + 1)); return
        fi
    done

    # 1. with the fix: the test must pass, or nothing below means anything
    rebuild "$dir" 0 || { echo "    INCONCLUSIVE: rebuild failed (see /tmp/vnc-build.log)"; fail=$((fail + 1)); return; }
    got=$(run_test "$test" "$dir" "$expect")
    if [ "$got" != PASS ]; then
        echo "    INCONCLUSIVE: the test does not pass WITH the fix in place ($got)."
        fail=$((fail + 1)); return
    fi
    echo "    with the fix    : PASS"

    # 2. without it: the test must fail
    if ! eval "$apply" 2>/dev/null; then
        echo "    INCONCLUSIVE: the inverse patch does not apply -- re-base it"
        restore; fail=$((fail + 1)); return
    fi
    rebuild "$dir" "$engine" || { echo "    INCONCLUSIVE: rebuild without the fix failed"; restore; rebuild "$dir" "$engine"; fail=$((fail + 1)); return; }
    got=$(run_test "$test" "$dir" "$expect")
    restore
    rebuild "$dir" "$engine"

    if [ "$got" = FAIL ]; then
        echo "    without the fix : FAIL   <- the control holds"
        pass=$((pass + 1))
    elif [ "$got" = SKIP ]; then
        echo "    INCONCLUSIVE: the test skipped itself without the fix"
        fail=$((fail + 1))
    else
        echo "    without the fix : PASS   <- THE CONTROL DOES NOT HOLD."
        echo "    The row claims this test catches the defect.  It does not."
        fail=$((fail + 1))
    fi
}

for row in "${ROWS[@]}"; do
    IFS='|' read -r sha test dir label <<<"$row"
    [ -n "$ONLY" ] && [ "$ONLY" != "$sha" ] && continue
    check_row "$sha" "$label" "$test" "$dir" fail \
              "git show '$sha' -- src/js | git apply -R -" 0
done

for row in "${PATCHES[@]}"; do
    IFS='|' read -r id pf test dir expect label <<<"$row"
    [ -n "$ONLY" ] && [ "$ONLY" != "$id" ] && continue
    pfile="t/tools/controls/$pf"
    if [ ! -f "$pfile" ]; then
        echo; echo "=== $id  $label"; echo "    INCONCLUSIVE: $pfile is missing"
        fail=$((fail + 1)); continue
    fi
    engine=0
    grep -q '^--- a/quickjs/' "$pfile" && engine=1
    check_row "$id" "$label" "$test" "$dir" "$expect" "git apply '$pfile'" "$engine"
done

echo
echo "================================================================"
printf 'verified %d   failed %d   skipped %d\n' "$pass" "$fail" "$skipped"

[ "$fail" -eq 0 ] || exit 1
exit 0
