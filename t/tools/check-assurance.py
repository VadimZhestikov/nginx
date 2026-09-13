#!/usr/bin/env python3
"""
V15 / SR-4 — check the assurance case against reality.

`js_comcon/docs-v5.0/ASSURANCE.md` is a claim -> assumption -> evidence tree. A tree
nobody checks decays into marketing at exactly the rate the code moves: this is the
document whose whole value is that its leaves point at things that exist, and it was
built AFTER discovering that 20 of 83 evidence citations in the doc set pointed at files
deleted during the convergence.

Six checks:

  [1] every EV: names an artifact that exists (or an old name the rename table redirects)
  [2] every leaf has evidence, or a GAP that says where it is owned
  [3] no ORPHAN evidence: every t/comcon_*.t is cited by some claim
  [4] coverage: every THREATS adversary T1..T12 and every V-item V1..V15 appears
  [5] no document in docs-v5.0 cites a t/... file that does not exist
  [6] every finding Fn referenced from a GAP exists in the findings table

Exit 0 = the case describes the tree that is actually there. Exit 1 = drift, printed.

    python3 t/tools/check-assurance.py [--verbose]
"""

import os
import re
import sys

VERBOSE = "--verbose" in sys.argv
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, "js_comcon", "docs-v5.0")
CASE = os.path.join(DOCS, "ASSURANCE.md")

fails = []


def note(msg):
    if VERBOSE:
        print("    " + msg)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


case = read(CASE)

# ---------------------------------------------------------------------------
# The rename table (§12): old cited name -> successor. A citation of a deleted
# test resolves only if the successor exists, so the redirect cannot itself rot.
# ---------------------------------------------------------------------------
renames = {}
for old, new in re.findall(
        r"^\|\s*`(t/[^`]+)`\s*\|\s*`(t/[^`]+)`\s*\|\s*`[0-9a-f]+`\s*\|$",
        case, re.M):
    renames[old] = new

note("rename table: %d entries" % len(renames))
if len(renames) < 5:
    fails.append("[1] the rename table in ASSURANCE.md §12 did not parse "
                 "(%d rows) -- check its column shape" % len(renames))

for old, new in sorted(renames.items()):
    if not os.path.exists(os.path.join(ROOT, new)):
        fails.append("[1] rename table sends %s -> %s, which does not exist"
                     % (old, new))


def resolve(path):
    """An artifact resolves if it is there, or is a redirected old name."""
    if os.path.exists(os.path.join(ROOT, path)):
        return True
    target = renames.get(path)
    return target is not None and os.path.exists(os.path.join(ROOT, target))


# ---------------------------------------------------------------------------
# Parse the tree: leaves are `#### G<n> — title`, fields are `- **KEY:** ...`
# ---------------------------------------------------------------------------
leaves = []
cur = None
open_field = None

for line in case.splitlines():
    m = re.match(r"^####\s+(G[0-9.]+)\s+—\s+(.*)$", line)
    if m:
        cur = {"id": m.group(1), "title": m.group(2),
               "ev": [], "gap": [], "threat": [], "v": []}
        leaves.append(cur)
        continue
    if cur is None:
        continue
    m = re.match(r"^-\s+\*\*(EV|GAP|THREAT|V|CLAIM|ARGUMENT):\*\*\s*(.*)$", line)
    if m:
        key, val = m.group(1), m.group(2)
        field = {"EV": "ev", "GAP": "gap", "THREAT": "threat",
                 "V": "v"}.get(key)
        if field:
            cur[field].append(val)
            open_field = field
        else:
            open_field = None
        continue

    # A wrapped bullet continues the field it belongs to: `home:` routinely
    # lands on the second line of a GAP, and reading only the first line
    # reported every one of them as unowned.
    if cur is not None and open_field and re.match(r"^\s+\S", line):
        cur[open_field][-1] += " " + line.strip()
    elif not line.strip():
        open_field = None

note("leaves: %d" % len(leaves))
if len(leaves) < 20:
    fails.append("[2] only %d leaves parsed from ASSURANCE.md -- the tree or its "
                 "heading shape changed" % len(leaves))

# --- [1] every EV artifact exists ------------------------------------------
print("[1] evidence artifacts exist")
cited = set()
for leaf in leaves:
    for ev in leaf["ev"]:
        m = re.match(r"`([^`]+)`", ev)
        if not m:
            fails.append("[1] %s: an EV line names no artifact in backticks: %s"
                         % (leaf["id"], ev[:60]))
            continue
        path = m.group(1)
        cited.add(path)
        if path.startswith("t/") or path.startswith("src/"):
            if not resolve(path):
                fails.append("[1] %s cites %s, which does not exist"
                             % (leaf["id"], path))
        else:
            # a doc reference: must be a file in the doc set
            if not os.path.exists(os.path.join(DOCS, path)):
                fails.append("[1] %s cites %s, which is not in docs-v5.0"
                             % (leaf["id"], path))
note("cited artifacts: %d" % len(cited))

# --- [2] every leaf has evidence or an owned gap ---------------------------
print("[2] every leaf carries evidence, or a gap with a home")
for leaf in leaves:
    if not leaf["ev"] and not leaf["gap"]:
        fails.append("[2] %s (%s) has neither EV nor GAP -- a claim with no "
                     "evidence and no admission that it lacks evidence is the "
                     "one thing this document exists to prevent"
                     % (leaf["id"], leaf["title"][:50]))
    for gap in leaf["gap"]:
        if "home:" not in gap:
            fails.append("[2] %s declares a GAP with no `home:` -- an unowned "
                         "gap is a gap nobody will close" % leaf["id"])

# --- [3] no orphan evidence ------------------------------------------------
print("[3] no orphan evidence (every comcon test belongs to a claim)")
tests = sorted(f for f in os.listdir(os.path.join(ROOT, "t"))
               if f.startswith("comcon_") and f.endswith(".t"))
for t in tests:
    if ("t/" + t) not in cited:
        fails.append("[3] t/%s is not cited by any claim -- either the tree is "
                     "incomplete or that test is not evidence for anything" % t)
note("comcon tests: %d, all cited: %s"
     % (len(tests), all(("t/" + t) in cited for t in tests)))

# --- [4] coverage of threats and V-items -----------------------------------
print("[4] coverage: every adversary and every V-item appears")
threats = set()
for leaf in leaves:
    for row in leaf["threat"]:
        threats.update(re.findall(r"\bT(\d+)\b", row))
vitems = set()
for leaf in leaves:
    for row in leaf["v"]:
        vitems.update(re.findall(r"\bV(\d+)[ab]?\b", row))

known_threats = set(re.findall(r"^### T(\d+) —", read(os.path.join(DOCS, "THREATS.md")), re.M))
note("THREATS defines: %s" % sorted(known_threats, key=int))
for t in sorted(known_threats, key=int):
    if t not in threats:
        fails.append("[4] adversary T%s from THREATS.md is not addressed by any "
                     "claim -- the completeness ledger has a hole" % t)

for n in range(1, 16):
    if str(n) not in vitems:
        fails.append("[4] V%d is not referenced by any claim -- a verification "
                     "obligation with no home in the case" % n)

# --- [5] no dangling test citation anywhere in the doc set -----------------
print("[5] no document cites a test file that does not exist")
for name in sorted(os.listdir(DOCS)):
    if not name.endswith(".md"):
        continue
    body = read(os.path.join(DOCS, name))
    for path in sorted(set(re.findall(r"\bt/[A-Za-z0-9_]+\.(?:t|sh|js|py)\b", body))):
        if not resolve(path):
            fails.append("[5] %s cites %s, which does not exist and has no "
                         "successor in the ASSURANCE.md rename table" % (name, path))

# --- [6] every finding referenced from a GAP is in the ledger --------------
print("[6] every finding named by a gap exists in the ledger")
ledger = set(re.findall(r"^\|\s*\*\*(F\d+)\*\*", case, re.M))
note("findings: %s" % sorted(ledger))
for leaf in leaves:
    for gap in leaf["gap"]:
        for f in re.findall(r"\bfinding (F\d+)", gap):
            if f not in ledger:
                fails.append("[6] %s points at %s, which is not in the findings "
                             "table" % (leaf["id"], f))

print("")
if fails:
    print("DRIFT (%d):" % len(fails))
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("the assurance case matches the tree that is actually there")
sys.exit(0)
