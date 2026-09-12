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
# WHAT IT WILL NOT DO.  It refuses to start on a dirty tree, it reverts nothing
# outside src/js, and it restores the tree on any exit including Ctrl-C.  If it
# ever leaves the tree modified, `git checkout -- src/js` is the whole recovery.
#
# FOUR ROWS NEED A MANUAL CHECK.  Later commits rewrote the same lines -- the two
# socket fixes supersede each other, and the numeric sweep collapsed the peer and
# SSL helpers onto one shared check -- so their inverse patch does not apply to
# today's tree.  The script names them and says what to do instead; it does not
# skip them silently.

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
"21b42d7e5|t/js_com_grant_declare.t|objs|grantToTenant conferred nothing"
"a321849fa|t/js_com_lb_select.t|objs|balancer with no return pinned peer 0"
"335dc0956|t/js_com_filter_nongenerator.t|objs|filter with no return dropped the response"
"67bc359e9|t/comcon_include_contract_fuzz.t|objs|malformed contract disabled admission"
)

MANUAL=(
"cd391a160|t/js_com_socket_fuzz.t|superseded by 07c7fd277, which rewrote the same lines"
"07c7fd277|t/js_com_socket_fuzz.t|builds on cd391a160; revert both together, newest first"
"5186565a1|t/js_com_peer_range.t|its helper was collapsed into ngx_js_com_num_range by e246cc347"
"0ebac7e47|t/js_com_ssl_range.t|same: its helper was collapsed by e246cc347"
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
