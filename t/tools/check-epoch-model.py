#!/usr/bin/env python3
"""
V10 — the epoch machinery, model-checked.

VERIFICATION.md §V10 asks for this and says why: the formal semantics is
single-threaded, while the mode fan-out is a fleet-wide protocol over shared
memory with concurrent writers, worker respawn and master reload.  "Monotone
rollout" was a slogan with no model behind it.

WHAT IS MODELLED is the protocol that actually ships, read off the JS bootstrap
in src/js/ngx_js_com.c -- `C.mode` and `modeReconcile` -- not an idealisation of
it:

    PRE-FIX          mode(m):  eff := m                  (local, immediate)
                               raw := shared.get(K)      <-- step 1
                               ep  := raw.epoch + 1
                               shared.set(K, {ep, m})    <-- step 2
                               myEpoch := ep

                   reconcile():  st := shared.get(K)
                                 if st.epoch == myEpoch: return
                                 eff := st.mode ; myEpoch := st.epoch

    SHIPPED          mode(m):  eff := m
                               ep  := publish(K, m)      <-- ONE step, under the
                                                             store's own lock
                               myEpoch := ep

                   reconcile():  if st.epoch <= myEpoch: return
                                 eff := st.mode ; myEpoch := st.epoch

THE READ AND THE WRITE BEING SEPARATE STEPS IS THE WHOLE FINDING, and modelling
them as one would have hidden it.  A model that cannot express the interleaving
cannot find a bug in it.

AND THE RECONCILER RULE ALONE DOES NOT FIX IT, which the model said before the
code was touched: the first attempt at a fix changed only the early return from
`==` to `<=`, and the model reported the identical 168 violations.  Once a lost
update has put a worker at the cell's epoch with a different mode, NO reading
rule can repair it -- the worker and the cell agree on the only thing a reader
compares.  The atomicity of the write is the load-bearing part; the reconciler
change is what makes the rollout monotone, which is a different property.

The checker runs BOTH protocols.  The fixed one must have no violation; the
pre-fix one must have one -- a model that reports "no violations" for a protocol
nobody has changed is measuring nothing, so the control is built in, the same
way the M-SES gate runs its battery unconfined.

Exhaustive over every interleaving of a small script: state space is tiny
because the finding needs two writers and one reader, and a bug that needs four
workers to appear would be a different bug.
"""

import itertools
import sys

MODES = ("audit", "enforce")


class State:
    __slots__ = ("cell", "eff", "ep", "pend")

    def __init__(self, nw, boot="enforce"):
        # the shared cell: None = absent (a zone that was re-created), else (epoch, mode)
        self.cell = None
        # per-worker effective mode and the epoch it believes it is at
        self.eff = [boot] * nw
        self.ep = [-1] * nw
        # a switch that has READ the cell but not yet WRITTEN it: (worker, read_epoch, mode)
        self.pend = [None] * nw

    def copy(self):
        s = State.__new__(State)
        s.cell = self.cell
        s.eff = list(self.eff)
        s.ep = list(self.ep)
        s.pend = list(self.pend)
        return s

    def key(self):
        return (self.cell, tuple(self.eff), tuple(self.ep), tuple(self.pend))


def step_read(s, w, mode):
    """mode(m) part one: apply locally, then READ the cell."""
    s.eff[w] = mode
    s.pend[w] = (s.cell[0] if s.cell else 0, mode)


def step_publish(s, w, mode):
    """The shipped `mode(m)`: read, increment and write in ONE critical section,
    the way ngx_js_shared_budget_charge and ngx_js_shared_cosign_record already
    do their read-modify-writes.  There is no interleaving point inside it."""
    s.eff[w] = mode
    ep = (s.cell[0] if s.cell else 0) + 1
    s.cell = (ep, mode)
    s.ep[w] = ep


def step_write(s, w):
    """mode(m) part two: WRITE epoch+1 with the mode this worker chose."""
    prev, mode = s.pend[w]
    s.pend[w] = None
    ep = prev + 1
    s.cell = (ep, mode)
    s.ep[w] = ep


def step_reconcile(s, w, monotone):
    if s.cell is None:
        return
    ce, cm = s.cell
    if monotone:
        if ce <= s.ep[w]:
            return
    else:
        if ce == s.ep[w]:
            return
    s.eff[w] = cm
    s.ep[w] = ce


def step_respawn(s, w, boot):
    """A worker that died and came back: fresh local state, config mode."""
    s.eff[w] = boot
    s.ep[w] = -1
    s.pend[w] = None


def violations(s, nw):
    """
    I2 -- THE UNRECOVERABLE STATE, and the one that matters.

    A worker whose epoch EQUALS the cell's while its mode DIFFERS can never be
    corrected: the pre-fix reconciler returns early on epoch equality, so the
    divergence is permanent and silent.  If the fleet was moved to `enforce` and
    one worker is left in `audit`, that worker is unshielded for the rest of its
    life and nothing says so.
    """
    out = []
    if s.cell is None:
        return out
    ce, cm = s.cell
    for w in range(nw):
        if s.ep[w] == ce and s.eff[w] != cm:
            out.append("I2 worker %d stuck at epoch %d in %s while the cell says %s"
                       % (w, ce, s.eff[w], cm))
    return out


def explore(script, nw, atomic, monotone, boot="enforce"):
    """
    `script` is a multiset of pending ACTIONS; every interleaving is explored.
    An action is ('switch', w, mode) -- which contributes TWO steps, a read and
    a write, that other workers' steps may fall between -- or ('reconcile', w)
    or ('respawn', w).
    """
    start = State(nw, boot)
    seen = set()
    found = []

    def walk(s, todo):
        k = (s.key(), tuple(sorted(todo)))
        if k in seen:
            return
        seen.add(k)

        v = violations(s, nw)
        if v:
            found.extend(v)
            return

        # a pending write may land at any point
        for w in range(nw):
            if s.pend[w] is not None:
                n = s.copy()
                step_write(n, w)
                walk(n, todo)

        for i, act in enumerate(todo):
            rest = todo[:i] + todo[i + 1:]
            if act[0] == "switch":
                _, w, m = act
                if s.pend[w] is not None:
                    continue            # one switch in flight per worker
                n = s.copy()
                if atomic:
                    step_publish(n, w, m)
                else:
                    step_read(n, w, m)
                walk(n, rest)
            elif act[0] == "reconcile":
                n = s.copy()
                step_reconcile(n, act[1], monotone)
                walk(n, rest)
            elif act[0] == "respawn":
                n = s.copy()
                step_respawn(n, act[1], boot)
                walk(n, rest)

    walk(start, tuple(script))
    return found


def main():
    nw = 3
    # Two concurrent switches in opposite directions, plus reconciles by every
    # worker, plus one respawn.  That is the smallest script that can express
    # "an operator tightens the fleet while another loosens it".
    script = [("switch", 0, "audit"), ("switch", 1, "enforce"),
              ("reconcile", 0), ("reconcile", 1), ("reconcile", 2),
              ("respawn", 2)]

    print("V10: the mode fan-out protocol, every interleaving")
    print("     workers=%d  actions=%d" % (nw, len(script)))
    print("")

    bad_fixed = explore(script, nw, atomic=True, monotone=True)
    bad_pre = explore(script, nw, atomic=False, monotone=False)
    # the fix that was NOT enough, kept as a third arm: it is the reason the
    # atomic publish exists rather than a one-line reader change
    bad_half = explore(script, nw, atomic=False, monotone=True)

    print("  PRE-FIX protocol (non-atomic publish, reconcile on EQUALITY):")
    if bad_pre:
        print("    %d violation(s), first: %s" % (len(bad_pre), bad_pre[0]))
    else:
        print("    none -- THE MODEL IS INERT")
    print("")
    print("  RECONCILER-ONLY fix (non-atomic publish, reconcile on GREATER):")
    if bad_half:
        print("    %d violation(s) -- STILL BROKEN, which is why the publish is atomic"
              % len(bad_half))
    else:
        print("    none -- unexpected; the atomic publish would then be unnecessary")
    print("")
    print("  SHIPPED protocol (atomic publish + reconcile on epoch GREATER):")
    if bad_fixed:
        print("    %d violation(s), first: %s" % (len(bad_fixed), bad_fixed[0]))
    else:
        print("    none")
    print("")

    fails = []
    if bad_fixed:
        fails.append("the shipped protocol violates I2: " + bad_fixed[0])
    if not bad_pre:
        fails.append("the model finds NO violation in the pre-fix protocol, so it "
                     "is not discriminating -- a model that passes everything "
                     "verifies nothing")
    if not bad_half:
        fails.append("the model finds no violation in the RECONCILER-ONLY fix, "
                     "which would mean the atomic publish is unnecessary; either "
                     "the model or the implementation is wrong")

    if fails:
        print("DRIFT (%d):" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1

    print("the epoch protocol holds under every interleaving this model explores,")
    print("and the model is shown to be capable of finding the defect it was")
    print("written for.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
