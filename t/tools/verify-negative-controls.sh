#!/usr/bin/env bash
#
# Verify the negative controls behind AUDIT_M-SES.md §2b.
#
# Every §2b row claims: revert this fix and the named test fails.  That is the
# claim which makes the row checkable by someone who did not write it, and this
# script is how you check it without taking anyone's word for the procedure.
#
# For each row it reverts ONLY the src/js half of the fix commit (the tests stay,
# or there would be nothing to run), rebuilds, runs the named test, and requires
# it to FAIL.  Then it restores, rebuilds, and requires it to PASS again.  A row
# passes only if the test fails without the fix and passes with it.
#
#   bash t/tools/verify-negative-controls.sh            # every row
#   bash t/tools/verify-negative-controls.sh 67bc359e9  # one row
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
# walks away is how a tree quietly stops being the tree you tested.  When the
# inverse patch no longer applies, the row belongs in MANUAL with a reason -- an
# explicit "revert this by hand" is worth more than an automated maybe.
#
# WHAT IT WILL NOT DO.  It refuses to start on a dirty tree, it reverts nothing
# outside src/js, and it restores the tree on any exit including Ctrl-C.  If it
# ever leaves the tree modified, `git checkout -- src/js` is the whole recovery.
#
# SOME ROWS NEED A MANUAL CHECK (the MANUAL list below).  Later commits rewrote
# the same lines -- the two socket fixes supersede each other, the numeric sweep
# collapsed the peer and SSL helpers onto one shared check, the F15 phases and
# the authoring tier's phases each rewrote the previous one's lines -- so their
# inverse patch does not apply to today's tree; and a few controls are not a
# commit revert at all (a one-line change by hand, or a fix in quickjs/, which
# this script does not touch).  The script names each and says what to do
# instead; it does not skip them silently.  NO BACKTICKS IN A ROW STRING: the
# rows are double-quoted, and a backtick there is a command substitution.

set -uo pipefail
cd "$(dirname "$0")/../.." || exit 2

BUILD=${BUILD:-objs}
NGINX="$PWD/$BUILD/nginx"
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
"37b3c2057|t/comcon_author_regrant.t|objs|the authoring tier, phase 3: re-granting by copy-then-narrow (without it a sub-fragment contract's grants are refused, and nothing narrows)"
"cabd6f4c2|t/comcon_deadline_without_worker.t|objs|comcon_rt had no interrupt handler at all before a worker existed, so config-phase evaluation could hang forever"
)

MANUAL=(
"cd391a160|t/js_com_socket_fuzz.t|superseded by 07c7fd277, which rewrote the same lines"
"07c7fd277|t/js_com_socket_fuzz.t|builds on cd391a160; revert both together, newest first"
"5186565a1|t/js_com_peer_range.t|its helper was collapsed into ngx_js_com_num_range by e246cc347"
"0ebac7e47|t/js_com_ssl_range.t|same: its helper was collapsed by e246cc347"
"21b42d7e5|t/js_com_grant_declare.t|the grant-wrapping path was rewritten by the uses/ttl budgets (2026-09-13); revert by hand against that"
"67bc359e9|t/comcon_include_contract_fuzz.t|the include contract path was rewritten by the refusal codes and the fail-closed tests check (2026-09-12)"
"d5c880cd4|t/comcon_leftover_accounting.t|the leftover-drain code it touched was rewritten by F14/F15's heap-walk and deadline changes (ffd76ed84, cabd6f4c2), landing the same session"
"ffd76ed84|t/comcon_invoke_heap_independence.t|__invokeConfined's deadline arithmetic it touched was rewritten by F15 phase 3 (cabd6f4c2), landing the same session"
"ffd76ed84|t/comcon_fragment_error_report.t|the include() exception-handling lines it touched were rewritten by F15 phase 2's compile-then-check restructuring (1618dce79), landing the same session"
"0317e8b90|t/comcon_global_binding_freeze.t|the compartment-setup lines it touched (the interrupt-handler install site) were rewritten by F15 phase 3 (cabd6f4c2), landing the same session"
"8c57ded4b|t/comcon_author_basic.t|the authoring tier, phase 2: its author.include() lines were rewritten by phase 3 (37b3c2057); revert both together, newest first"
"b75e7d9e5|t/comcon_author_basic.t|phase 1 (the bounds as a stack): its push/pop lines were rewritten by phase 2's stage split (8c57ded4b); by hand, drop the two min() lines in ngx_js_comcon_deadline_push()/mem_push() and /nestdeadline and /nestmemory must fail"
"by-hand|t/comcon_author_regrant.t|copy-vs-rewrap: replace *child = *op in ngx_js_socket_narrow() with ngx_js_socket_wrap_bounded(op->handle, ...) and the /stale arm must fail (a re-grant from the stale parent comes out fresh)"
"by-hand|t/comcon_author_basic.t|the nested JSON marshal: return the result value from ngx_js_author_invoke() without the stringify/parse and the returnsFn/toJSON/throws assertions must fail (an object crosses)"
"by-hand|t/comcon_jit_uncatchable.t|F16 lives in quickjs/ (outside src/js): drop the JS_IsUncatchableException(ctx) guard from quickjs-jit.c's _ex: dispatch, rebuild the lib and objs_jit, and the test must report SURVIVED on objs_jit"
"by-hand|t/run_sanitizers.sh|F17 (a): remove the ngx_js_comcon_teardown(jcf) call from ngx_js_exit_process, rebuild objs_asan, run the sanitizer script: every comcon file reports a src/js leak frame (ngx_js_comcon_publish)"
"by-hand|t/run_sanitizers.sh|F17 (b): remove the ngx_js_http_register_classes/ngx_js_upstream_register_classes calls from ngx_js_com_register_classes (and restore them in ngx_js_com_init), rebuild objs_asan, run the script: comcon_v12_denial_codes.t reports ngx_js_wrap_server"
"by-hand|t/comcon_oom_backtrace.t|F18 lives in quickjs/ (outside src/js): in quickjs.c replace the build_backtrace_pending(ctx, NULL, 0, 0, 0) call at JS_CallInternal's exception label with build_backtrace(ctx, rt->current_exception, NULL, 0, 0, 0), rebuild the lib and objs, and the sweep's first response is empty (the worker died on signal 11): three of five fail"
"1618dce79|t/comcon_wrapper_breakout.t|the JS_EvalFunction()/JS_Call() lines it touched were rewritten by F15 phase 3's deadline push/pop (cabd6f4c2), landing the same session"
)

if ! git diff --quiet -- src/js || ! git diff --cached --quiet -- src/js; then
    echo "REFUSING: src/js has uncommitted changes."
    echo "This script reverts and restores src/js; it will not run over your work."
    exit 2
fi

restore() {
    git checkout -- src/js 2>/dev/null
}
trap restore EXIT INT TERM

rebuild() {  # $1 = builddir
    make -f "$1/Makefile" build -j"$(nproc)" >/tmp/vnc-build.log 2>&1
}

# run $1=test $2=builddir ; echo PASS or FAIL
run_test() {
    local t="$1" d="$2" out
    if [ "$d" = objs_ubsan ]; then
        # this row's control is a SANITIZER finding, not a failing assertion
        rm -f /tmp/vnc-ubsan.*
        UBSAN_OPTIONS="log_path=/tmp/vnc-ubsan:halt_on_error=0" \
            TEST_NGINX_BINARY="$PWD/$d/nginx" prove "$t" >/dev/null 2>&1
        if cat /tmp/vnc-ubsan.* 2>/dev/null | grep -q "src/js"; then
            echo FAIL          # a finding in src/js == the defect is present
        else
            echo PASS
        fi
        return
    fi
    out=$(TEST_NGINX_BINARY="$PWD/$d/nginx" prove "$t" 2>&1)
    if echo "$out" | grep -q "^Result: PASS"; then echo PASS; else echo FAIL; fi
}

pass=0; fail=0; skipped=0

for row in "${ROWS[@]}"; do
    IFS='|' read -r sha test dir label <<<"$row"
    [ -n "$ONLY" ] && [ "$ONLY" != "$sha" ] && continue

    echo
    echo "=== $sha  $label"
    echo "    test: $test   build: $dir"

    if [ ! -x "$dir/nginx" ]; then
        echo "    SKIP: $dir is not configured here"
        skipped=$((skipped + 1)); continue
    fi

    # 1. with the fix: the test must pass, or nothing below means anything
    rebuild "$dir"
    if [ "$(run_test "$test" "$dir")" != PASS ]; then
        echo "    INCONCLUSIVE: the test does not pass WITH the fix in place."
        fail=$((fail + 1)); continue
    fi
    echo "    with the fix    : PASS"

    # 2. without it: the test must fail
    if ! git show "$sha" -- src/js | git apply -R - 2>/dev/null; then
        echo "    INCONCLUSIVE: the inverse patch does not apply (see MANUAL below)"
        restore; skipped=$((skipped + 1)); continue
    fi
    rebuild "$dir"
    got=$(run_test "$test" "$dir")
    restore
    rebuild "$dir"

    if [ "$got" = FAIL ]; then
        echo "    without the fix : FAIL   <- the control holds"
        pass=$((pass + 1))
    else
        echo "    without the fix : PASS   <- THE CONTROL DOES NOT HOLD."
        echo "    The row claims this test catches the defect.  It does not."
        fail=$((fail + 1))
    fi
done

echo
echo "================================================================"
printf 'verified %d   failed %d   skipped %d\n' "$pass" "$fail" "$skipped"

if [ -z "$ONLY" ]; then
    echo
    echo "MANUAL — later commits rewrote these lines, so the inverse patch"
    echo "does not apply.  Revert by hand to check them:"
    for row in "${MANUAL[@]}"; do
        IFS='|' read -r sha test why <<<"$row"
        printf '  %-10s %-28s %s\n' "$sha" "$test" "$why"
    done
fi

[ "$fail" -eq 0 ] || exit 1
exit 0
