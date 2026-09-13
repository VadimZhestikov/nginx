#!/usr/bin/env python3
"""
Hunt assertions that CANNOT FAIL.

This arc found five of them, every one by accident while doing something else:

  * the `realize` escape probe forged a 4th argument to a 3-argument operator,
    so the forgery was discarded and the probe passed on every build;
  * the `cap.expired` golden-corpus row reported "alive" forever, because the
    row included its fragment after the sleep -- and nothing pinned the row, so
    the suite stayed green;
  * the `Symbol.for` cross-identity arm read
    `Symbol.for(k) === Symbol.for(k) ? 'SHARED' : 'clean'` -- two calls in one
    fragment, compared with each other, which cannot come out 'clean';
  * the cross-identity coverage count scanned the whole payload for
    "hostChannel", so adding an unrelated arm made it read 8-of-8 for a battery
    of seven;
  * the assurance ledger's F9 row listed five unbuilt V-items where the
    placement table has always shown six.

Every one of those was green. That is the shape of the problem: a dead probe
does not fail, it reassures. V11 mutation-tests the POLICIES; nothing has ever
tested the TESTS. This is the cheap structural half of that -- it cannot prove
an assertion is live, but it finds the shapes that are dead by construction.

WHAT IT CANNOT DO, stated up front so nobody credits it with more. It is a
static reader. It cannot tell whether an assertion's subject is reachable, nor
whether a control fires; only running a mutation can do that (see
`t/tools/verify-negative-controls.sh`, which does it for named fixes). A clean
run here means "no assertion is dead in one of the ways we have already been
burned by", never "every assertion is live".

Exit non-zero on any finding. Run from the repo root.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

findings = []


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def tests():
    d = os.path.join(ROOT, "t")
    return sorted(f for f in os.listdir(d) if f.endswith(".t"))


def lineno(text, pos):
    return text.count("\n", 0, pos) + 1


# ---------------------------------------------------------------------------
# [1] A comparison of an expression with ITSELF.
#
# The Symbol arm's exact shape. `a === a` is true for every value except NaN,
# so as the read of a probe it is a constant.
#
# `a !== a` is the IDIOMATIC NaN TEST and is deliberately not reported -- the
# check that flags a real bug and a real idiom alike gets switched off.
# ---------------------------------------------------------------------------
SELF_CMP = re.compile(
    r"([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*\([^()]*\))"
    r"\s*===\s*"
    r"([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*\([^()]*\))"
    r"(?P<tail>[^;\n]{0,80})"
)


def strip_commentary(text):
    """Blank out Perl comment lines and JS block comments.

    Without this the checker reports the PROSE that documents a dead probe as a
    dead probe -- which it did on its first run, twice, in the very file whose
    comment explains the bug it was written to find.  A checker that cannot tell
    code from a description of code will be switched off within a week.
    """
    out = []
    for line in text.split("\n"):
        out.append("" if line.lstrip().startswith("#") else line)
    text = "\n".join(out)
    return re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text,
                  flags=re.S)


def check_self_comparison():
    print("[1] a DISCRIMINATOR that compares an expression with itself")
    js = [x for x in sorted(os.listdir(os.path.join(ROOT, "t", "tools")))
          if x.endswith(".js")]
    for f in tests() + ["tools/" + x for x in js]:
        text = strip_commentary(read(os.path.join("t", f)))
        for m in SELF_CMP.finditer(text):
            a, b = m.group(1).strip(), m.group(2).strip()
            if a != b:
                continue
            tail = m.group("tail")
            # A self-comparison is only DEAD when its result is used to tell two
            # outcomes apart -- `f(x) === f(x) ? 'SHARED' : 'clean'`, the shape
            # of the Symbol arm.  The SAME expression is a legitimate
            # DETERMINISM assertion when it is paired with a discriminating
            # clause (`&& f(77) !== f(78)`), which is how both fuzz generators
            # in this tree check that a seed reproduces.  Reporting those would
            # be reporting the correct use of the idiom.
            if "!==" in tail or "!=" in tail:
                continue
            if not re.search(r"\?\s*['\"][^'\"]+['\"]\s*:\s*['\"][^'\"]+['\"]", tail):
                continue
            findings.append(
                "[1] t/%s:%d  `%s === %s` decides between two labels -- two "
                "calls are not two observations, so the label is a constant"
                % (f, lineno(text, m.start()), a, b))


# ---------------------------------------------------------------------------
# [2] Assertions that are true by construction.
# ---------------------------------------------------------------------------
TAUTOLOGY = [
    (re.compile(r"\bok\(\s*1\s*[,)]"),            "ok(1) always passes"),
    (re.compile(r"\bok\(\s*!\s*0\s*[,)]"),        "ok(!0) always passes"),
    (re.compile(r"\bok\(\s*defined\s+1\s*[,)]"),  "ok(defined 1) always passes"),
    (re.compile(r"\bcmp_ok\([^,]+,\s*'>='\s*,\s*0\s*[,)]"),
     "a count is compared '>= 0', which no count can fail"),
    (re.compile(r"\bis\(\s*(\$\w+)\s*,\s*\1\s*[,)]"),
     "is($x, $x) compares a value with itself"),
    (re.compile(r"\blike\(\s*[^,]+,\s*qr/\s*/"),
     "like() against an empty pattern matches anything"),
]


def check_tautologies():
    print("[2] assertions true by construction")
    for f in tests():
        # Commentary stripped here for the SAME reason as in [1], and the
        # repeat is the point: the first version of this check reported four
        # findings that were the sentences explaining the defect it had just
        # helped fix ("this was an ok(1) asserting nothing").  Any checker that
        # reads source as text has to be told where the code is, once per check,
        # and forgetting it in one place produces findings that look exactly
        # like the real thing.
        text = strip_commentary(read(os.path.join("t", f)))
        for rx, why in TAUTOLOGY:
            for m in rx.finditer(text):
                ln = lineno(text, m.start())
                # An `ok(1, ...)` used as a RECON marker is still a dead
                # assertion; it is reported, and the fix is to delete it or
                # assert something.
                findings.append("[2] t/%s:%d  %s" % (f, ln, why))


# ---------------------------------------------------------------------------
# [3] Golden-corpus rows that no assertion pins.
#
# The `cap.expired` defect: a frozen contract whose rows nothing checks is
# decoration. Every row's code/name must appear somewhere in t/ outside the
# corpus file itself.
# ---------------------------------------------------------------------------
CORPORA = {
    "tools/golden-denials.js": r"code:\s*'([^']+)'",
    "tools/mses-probes.js":    r"^\s*\"?\s*([a-z][a-z_0-9]*):\s*p\(",
    "tools/erasure-corpus.js": r"name:\s*'([^']+)'",
    # policy-mutants builds its rows through a helper (`m = {label: ...}`),
    # so the row identities are the LABEL arguments at the call sites, not a
    # `name:` field.  Scraping the field a sibling corpus happens to use is how
    # this checker drifted from a corpus on its own first run.
    "tools/policy-mutants.js": r"mut\(\s*'([^']+)'",
}


def check_corpora():
    """A corpus with no reader, or a scraper that no longer matches its rows.

    THE EARLIER VERSION OF THIS CHECK WAS NOISE and is recorded here so it is
    not rewritten: it asked whether each row's name appears in a t/ assertion,
    and reported twenty rows that are perfectly well pinned -- because the
    harnesses ITERATE their corpus and assert per row instead of spelling each
    name out in Perl.  "Not named in Perl" is not "unchecked", and a checker
    that conflates them costs more attention than it saves.

    What IS checkable statically: a corpus nobody reads, and a scraper that has
    drifted from the shape of the rows it scrapes.  The second one fired on this
    check's first run -- against ITSELF: the mses-probes regex looked for
    `name:` where that file keys its probes `c_fn_ctor: p(...)`.  The per-row
    liveness question is dynamic and belongs where it can actually be answered:
    the consuming test asserting that every row REPORTED an outcome, which is
    what `t/comcon_v12_denial_codes.t` gained when `cap.expired` was found
    reporting "alive" forever.
    """
    print("[3] corpora: a reader exists, and the scraper still matches the rows")
    consumers = dict((f, read(os.path.join("t", f))) for f in tests())
    for rel, rx in CORPORA.items():
        if not os.path.exists(os.path.join(ROOT, "t", rel)):
            continue
        base = os.path.basename(rel)
        if not any(base in c for c in consumers.values()):
            findings.append("[3] t/%s is read by no test -- a corpus nobody "
                            "runs is a file, not a contract" % rel)
        rows = re.findall(rx, read(os.path.join("t", rel)), re.M)
        if not rows:
            findings.append(
                "[3] t/%s: the row scraper %r matches nothing -- this checker "
                "and the corpus have drifted, so any per-row claim it makes "
                "below is vacuous" % (rel, rx))
        else:
            note = " (%d rows)" % len(set(rows))
            print("    %s%s" % (rel, note))


# ---------------------------------------------------------------------------
# [4] A count parsed from the WHOLE payload, then described as a fixed total.
#
# The cross-identity coverage bug: the count scanned every "hostChannel" in the
# response, so adding an unrelated arm made a battery of seven report 8-of-8. A
# global match feeding a message that names a constant is the shape.
# ---------------------------------------------------------------------------
GLOBAL_COUNT = re.compile(r"=\s*\(\s*(\$\w+)\s*=~\s*/([^/]+)/g\s*\)")


def check_global_counts():
    """A /g tally over the WHOLE response, described as a fixed total.

    The cross-identity coverage bug: the count scanned every "hostChannel" in
    the payload, so adding an unrelated arm made a battery of seven report
    8-of-8.  A tally that grows when you add something else is not a tally.

    Scoped to the RAW payload only.  The fix for that bug was to count inside a
    sub-extract (`$probe_block`), and a checker that still reported the fixed
    code would be teaching people to ignore it.  The raw payload is recognised
    by being the variable the file also passes to `like()` -- derived extracts
    are not asserted on directly.
    """
    print("[4] a coverage tally scraped from the whole payload")
    words = {"two": 2, "three": 3, "four": 4, "five": 5, "six": 6,
             "seven": 7, "eight": 8, "nine": 9, "ten": 10, "eleven": 11,
             "twelve": 12}
    for f in tests():
        text = read(os.path.join("t", f))
        asserted = set(re.findall(r"like\(\s*(\$\w+)", text))
        for m in GLOBAL_COUNT.finditer(text):
            var = m.group(1)
            if var not in asserted:
                continue          # a scoped extract, which is the fix, not the bug
            tail = text[m.end():m.end() + 800]
            named = [w for w in words if re.search(r"\bof the %s\b" % w, tail)]
            if not named:
                continue
            findings.append(
                "[4] t/%s:%d  a /g tally over %s (the whole payload) is "
                "described as 'of the %s' -- another arm emitting the same key "
                "moves the total and the claim changes silently"
                % (f, lineno(text, m.start()), var, named[0]))


check_self_comparison()
check_tautologies()
check_corpora()
check_global_counts()

print("")
if findings:
    print("DEAD-PROBE FINDINGS (%d):" % len(findings))
    for x in findings:
        print("  - " + x)
    sys.exit(1)
print("no assertion is dead in any of the four ways this tree has been burned by")
sys.exit(0)
