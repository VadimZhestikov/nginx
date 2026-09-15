# REVIEW — what a second signer does

**This document exists because of finding F11.** `AUDIT_M-SES.md` §5 and
`ASSURANCE.md` §15 each carry one signer, and both signature rows say so in their
own words: *"the §4 commands were executed by the authoring session, not
independently re-run by the signer"*. What exists is therefore **acceptance of
reproducible evidence, not a reproduction**. Assumption A2 accepts that as
residual risk, and F11 records it as the gap no amount of engineering closes —
because what is missing is a second pair of eyes, not an artifact.

The gap was never missing evidence. It was that reproducing it meant reading two
documents of about eleven hundred lines, extracting the commands by hand, knowing
which build directories are stale, and judging for yourself which numbers matter.
That work is now done once, here.

---

## 1. Run the evidence

```bash
bash t/tools/reviewer-pack.sh            # everything, about 40 minutes
bash t/tools/reviewer-pack.sh --quick    # suites + checkers only, about 8
```

It refuses rather than proceeding if the tree has uncommitted changes under
`src/js`, `quickjs`, `t` or `js_comcon`: **a signature has to name a commit, and
evidence gathered from a modified tree names nothing.** It writes its transcript
outside the repository, so running it cannot dirty the tree it just checked.

It rebuilds every build directory first, and that is not a courtesy. The `objs*/`
trees are **tracked in git**, so a fresh clone or a `git reset --hard` hands you a
*committed* binary rather than one built from the source you are auditing.
Assembling the audit, exactly that produced a FAIL on `objs_jit` that the real
build passes — and the reverse case is worse, because it would have been a pass
nobody measured. The pack also checks that each binary is newer than the newest
source file, which is the mechanical form of the same warning.

**Two classes of result, and the separation is the point.**

- **GATE** — objectively pass or fail, and the script's exit code depends on it: a
  suite passes or it does not, a sanitizer reports findings in `src/js` or it does
  not, a checker exits zero or it does not, the negative controls hold or they do
  not.
- **REPORTED** — numbers to look at, which the script deliberately does not judge:
  file and test counts, the leaf count, the delta-log version. They drift
  legitimately as tests are added, so gating on them would fail for the wrong
  reason, and pinning them here would create a second copy of every count outside
  the documents that own them.

A `--quick` run attests less than a full one. §4's block below has a line for
saying which you did; please use it.

---

## 2. Read what you would be accepting

**The pack does not summarise the residuals, on purpose.** A signature on a
summary is worth less than no signature, and a document that copies a table
acquires a second place for that table to be wrong. So the lists stay where they
live, and a signer reads them there:

| read | for |
|---|---|
| `ASSURANCE.md` §15 | what the first signature attests, and **what would invalidate it** |
| `ASSURANCE.md` §16 | everything that changed *after* that signature — it is long, and it is the part a reader is most likely to skip |
| `ASSURANCE.md` findings ledger | the residuals themselves. **F8** (timing channels, quantified) and **F11** (this gap) are ACCEPTED, not closed |
| `ASSURANCE.md` §14 | what this case does not establish. That statement is part of what is signed |
| `AUDIT_M-SES.md` §3 | the five M-SES gaps, accepted as residual risk. One row carries an **ERRATUM**: a residual that was withdrawn because the probe that found it compared a value with itself |
| `AUDIT_M-SES.md` §6 | changes to the audited surface after that signature |

From §15, in its own words: *"A signature on an assurance case is an acceptance of
the residuals it names. A signature applied without reading it converts 'we know
these holes exist' into 'someone looked and found nothing', which is worth less
than no signature at all."*

---

## 3. Two things worth knowing before you sign

**The instruments have themselves been wrong.** This project has found five
assertions that could not fail, three dead probes, two miscounted coverage
numbers, and one flake in its own test suite at four percent — most of them by
accident while doing something else. `check-dead-probes.py` now gates four of
those shapes and `verify-negative-controls.sh` exercises the fixes' tests against
reverted fixes, which is why both are in the pack. Neither proves an instrument
measures what it claims; they rule out specific ways of measuring nothing.

**Nineteen negative-control rows cannot be checked automatically** (six when
this was first written). Their inverse patches no longer apply, because later
commits rewrote the lines they target — or the control was never a commit revert
(a one-line change by hand, or a fix in `quickjs/`, outside what the script
reverts). The pack prints them; each names the reason and the exact change to
make. Checking one means reverting it by hand. That is maintenance debt the
existing signature accepted, and it is still outstanding — labelled, not paid.

**Do not write into `src/js`, `quickjs`, `t` or `js_comcon` while the pack runs.**
Its dirty-tree check happens once, at the start; a file that appears afterwards
is run and cited as if it were part of the tree. One run was spoiled that way.

---

## 4. Sign-off

Fill this in, attach the transcript, and commit it. If you are not prepared to
attest a line, strike it out rather than softening it — the value of the row
below is entirely in its being narrow and true.

| role | signer | date | commit | run | attests |
|---|---|---|---|---|---|
| Independent reproduction (F11) | *(name)* | *(date)* | *(git rev-parse HEAD)* | full / --quick | I ran `reviewer-pack.sh` myself on the commit named, all gates passed, and I have read §2's documents including the findings ledger. I accept the residuals named there. This is an independent reproduction of the evidence, not a review of the design. |

**What signing this does and does not do.** It closes the *reproduction* half of
F11: the evidence has now been run by someone other than the authoring session.
It does **not** make the assurance case two attestations of the *design* — that
would need a reviewer who disagrees with the argument and says where, which is a
different and larger exercise. Recording the narrower claim honestly is worth
more than implying the broader one.
