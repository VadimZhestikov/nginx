#!/usr/bin/env python3
"""
V7 — enumerations are generated, never maintained (VERIFICATION.md).

Three enumerations in this design are CLOSED, and their *completeness* is
load-bearing: the p_symbol kinds (a node kind that means two things at two tiers
is a wrong answer that looks right), the COMPILE PORTALS (every path that turns
text into code -- the "no hidden compile path" claim), and the OPS-RESOURCE
capabilities (the "no backdoor" claim of FOUNDATION §8a).

Hand-maintained lists rot, and the rot is silent: nothing fails when a list stops
matching the code it describes. So this derives each list FROM THE SOURCE and
fails on drift. It is wired into t/comcon_enumerations.t, so drift breaks the
suite rather than waiting for someone to re-read a document.

    python3 t/tools/check-enumerations.py [--verbose]

Exit 0 = every enumeration agrees with the code. Exit 1 = drift, printed.
"""

import re
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VERBOSE = "--verbose" in sys.argv or "-v" in sys.argv
fails = []


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return f.read()


def note(msg):
    if VERBOSE:
        print("    " + msg)


# ---------------------------------------------------------------------------
# 1. p_symbol kinds: schema comcon-pom-1, one numbering across BOTH tiers.
#
# The C enum is canonical (it is what the bytecode tier emits). The JS selector
# layer maps the same names, and POM.md documents the schema. All three must
# agree -- and the JS side is exactly where it drifted once already: a
# FunctionDeclaration reported kind 4 (stmt) because its type name ends in
# "Declaration", so query('function') matched at one tier and silently returned
# nothing at the other for the same function.
# ---------------------------------------------------------------------------
def check_p_symbols():
    print("[1] p_symbol kinds (schema comcon-pom-1)")
    qjs = read("quickjs/quickjs.c")

    c_kinds = dict(
        (m.group(1).lower(), int(m.group(2)))
        for m in re.finditer(r"NGX_COMCON_POM_([A-Z]+)\s*=\s*(\d+)", qjs)
    )
    if not c_kinds:
        fails.append("[1] no NGX_COMCON_POM_* enum found in quickjs.c")
        return
    note("C enum: %s" % c_kinds)

    expected = {"module": 1, "function": 2, "block": 3, "stmt": 4, "expr": 5}
    if c_kinds != expected:
        fails.append("[1] C enum %s != schema comcon-pom-1 %s (append only, "
                     "never renumber)" % (c_kinds, expected))

    # POM.md documents the same numbering in prose.
    pom = read("js_comcon/docs-v5.0/POM.md")
    doc = dict(
        (m.group(1), int(m.group(2)))
        for m in re.finditer(r"\b(module|function|block|stmt|expr)=(\d)", pom)
    )
    note("POM.md: %s" % doc)
    for name, num in c_kinds.items():
        if doc.get(name) != num:
            fails.append("[1] POM.md documents %s=%s, C says %s"
                         % (name, doc.get(name), num))

    # The JS selector layer maps the same names to the same numbers.
    com = read("src/js/ngx_js_com.c")
    js = dict(
        (m.group(1), int(m.group(2)))
        for m in re.finditer(r"f===\\'(module|function|block|stmt|expr)\\'\)"
                             r"return node\.kind===(\d)", com)
    )
    if not js:
        js = dict(
            (m.group(1), int(m.group(2)))
            for m in re.finditer(r"f==='(module|function|block|stmt|expr)'\)"
                                 r"return node\.kind===(\d)", com)
        )
    note("JS pomFactor: %s" % js)
    if len(js) != 5:
        fails.append("[1] pomFactor maps %d of 5 kind names (%s) -- a selector "
                     "that cannot name a kind cannot query it"
                     % (len(js), sorted(js)))
    for name, num in js.items():
        if c_kinds.get(name) != num:
            fails.append("[1] pomFactor maps %s to %s, C enum says %s"
                         % (name, num, c_kinds.get(name)))

    # cstKind must only ever produce numbers from the schema.
    m = re.search(r"function cstKind\(t\)\{(.*?)return 5;\}", com, re.S)
    if not m:
        fails.append("[1] cstKind not found in the bootstrap")
    else:
        used = set(int(x) for x in re.findall(r"return (\d);", m.group(1) + "return 5;"))
        note("cstKind returns: %s" % sorted(used))
        outside = used - set(c_kinds.values())
        if outside:
            fails.append("[1] cstKind returns kinds outside the schema: %s"
                         % sorted(outside))


# ---------------------------------------------------------------------------
# 2. Compile portals: every place src/js turns text into code.
#
# "No hidden compile path" is a security claim, and it is only checkable if the
# list is complete. Keyed by (file, enclosing function) rather than line number,
# so ordinary edits do not churn it; a NEW compiling function is a failure that
# must be justified by adding a row here, with what it compiles.
# ---------------------------------------------------------------------------
PORTALS = {
    # comcon / admission
    ("ngx_js_module.c", "ngx_js_comcon_include_confined"):
        "x2: a confined fragment (wrapped in NGX_JS_COMCON_WRAP_*) and the "
        "contract's admission TEST function, compiled in the compartment",
    ("ngx_js_module.c", "ngx_js_comcon_eval_dep"):
        "a pinned pure-library dependency, sha256-checked before it compiles",
    ("ngx_js_module.c", "ngx_js_comcon_parse"):
        "the vendored acorn parser, evaluated once on first use",
    ("ngx_js_com.c", "ngx_js_com_init"):
        "x3: the comcon capability-layer bootstrap plus two COM prototype "
        "helper scripts",
    ("ngx_js_com.c", "ngx_js_eval_module"):
        "x2: a host-JS ES module -- compile (JS_Eval) then run "
        "(JS_EvalFunction); this is the js_source root path",
    # host surfaces
    ("ngx_js_http_module.c", "ngx_js_request_install_proto"):
        "x2: the request hook-chain runners (chain and after)",
    ("ngx_js_listener.c", "ngx_js_l4_install_source_factory"):
        "the L4 listener source-factory helper",
    ("ngx_js_sw.c", "ngx_js_sw_thread"): "the SharedWorker entry script",
    ("ngx_js_worker.c", "ngx_js_worker_thread"): "the Worker entry script",
    # operator REPL (P19 substrate) -- an operator session IS a compile portal
    ("ngx_js_repl.c", "ngx_js_repl_eval"): "x2: an operator REPL line",
    ("ngx_js_repl.c", "ngx_js_repl_is_complete"):
        "compile-only, to decide whether a REPL line is finished",
    ("ngx_js_repl.c", "ngx_js_repl_attach"): "the REPL attach helper",
    ("ngx_js_repl.c", "ngx_js_repl_detach"): "the REPL detach helper",
}



def check_portals():
    print("[2] compile portals (every path from text to code in src/js)")
    found = {}
    srcdir = os.path.join(ROOT, "src/js")
    for fn in sorted(os.listdir(srcdir)):
        if not fn.endswith(".c"):
            continue
        cur = "?"
        for line in read("src/js/" + fn).split("\n"):
            m = re.match(r"^([a-zA-Z_][a-zA-Z0-9_]*)\(", line)
            if m:
                cur = m.group(1)
            stripped = line.lstrip()
            if stripped.startswith("*") or stripped.startswith("/*") \
               or stripped.startswith("//"):
                continue          # prose mentioning JS_Eval is not a portal
            if re.search(r"\bJS_Eval(Function)?\(", line):
                found.setdefault((fn, cur), 0)
                found[(fn, cur)] += 1
    note("found %d compiling functions, %d call sites"
         % (len(found), sum(found.values())))
    for k in sorted(found):
        note("  %s:%s  x%d" % (k[0], k[1], found[k]))

    new = [k for k in found if k not in PORTALS]
    gone = [k for k in PORTALS if k not in found]
    for k in sorted(new):
        fails.append("[2] UNENUMERATED compile portal %s:%s -- every path that "
                     "turns text into code must be listed in PORTALS with what "
                     "it compiles, or the no-hidden-compile-path claim is not "
                     "checkable" % k)
    for k in sorted(gone):
        fails.append("[2] enumerated portal %s:%s no longer exists -- remove "
                     "the row so the list keeps meaning something" % k)


# ---------------------------------------------------------------------------
# 3. Ops-resource capabilities (FOUNDATION §8a, the "no backdoor" claim).
#
# The code is canonical: std.ops builds its session from OPS_RES, so that table
# is what actually decides which verbs exist. The DOC is what a reader trusts, so
# every resource in the code must appear in §8a -- otherwise the prose quietly
# describes a smaller kernel than the one that ships.
# ---------------------------------------------------------------------------
def check_ops_resources():
    print("[3] ops-resource capabilities (FOUNDATION §8a)")
    com = read("src/js/ngx_js_com.c")
    m = re.search(r'"  var OPS_RES=\{"(.*?)"  STD\.ops=function', com, re.S)
    if not m:
        fails.append("[3] OPS_RES table not found in the bootstrap")
        return
    keys = re.findall(r'"\s+([a-z]+):\{doc:', m.group(1))
    note("OPS_RES: %s" % keys)
    if len(keys) < 7:
        fails.append("[3] OPS_RES has %d entries; §8a enumerates seven host "
                     "resources -- a missing one is a verb that cannot be "
                     "governed" % len(keys))

    found_doc = re.search(r"## 8a\..*?\n## ", read("js_comcon/docs-v5.0/FOUNDATION.md"),
                          re.S)
    if not found_doc:
        fails.append("[3] FOUNDATION §8a not found")
        return
    body = found_doc.group(0)
    aliases = {
        "log": ["denial/observation log"],
        "learn": ["learning-recorder", "learning recorder"],
        "mode": ["audit/enforce", "mode switch"],
        "bindings": ["binding/epoch store"],
        "broadcast": ["class-F broadcast channel"],
        "snapshot": ["snapshot store"],
        "provenance": ["provenance/grant-chain registry", "grant-chain"],
        "signing": ["signing key"],
        "sessions": ["session registry"],
    }
    for k in keys:
        alts = aliases.get(k, [k])
        if not any(a in body for a in alts):
            fails.append("[3] resource %r is in the code but §8a does not "
                         "mention it (tried %s) -- the doc would describe a "
                         "smaller kernel than ships" % (k, alts))


# ---------------------------------------------------------------------------
# 4. The C3 intrinsics allowance: one list, two copies.
#
# The engine decides admission from ngx_js_admit_intrinsics[]; the V3 kernel
# oracle predicts admission from its own copy. If they drift, the oracle stops
# describing the engine and the differential test agrees with itself -- the
# exact failure V3 was built to avoid, arriving through the back door. So the
# two are compared here, and a difference is drift in either direction.
# ---------------------------------------------------------------------------
def check_intrinsics():
    print("[4] C3 intrinsics allowance (engine vs the V3 oracle)")
    com = read("src/js/ngx_js_com.c")
    m = re.search(r"ngx_js_admit_intrinsics\[\]\s*=\s*\{(.*?)NULL", com, re.S)
    if not m:
        fails.append("[4] ngx_js_admit_intrinsics[] not found")
        return
    c_list = set(re.findall(r'"([A-Za-z]+)"', m.group(1)))

    orc = read("t/tools/kernel-oracle.js")
    m2 = re.search(r"var INTRINSIC = \{(.*?)\};", orc, re.S)
    if not m2:
        fails.append("[4] INTRINSIC table not found in the kernel oracle")
        return
    js_list = set(re.findall(r"([A-Za-z]+)\s*:\s*1", m2.group(1)))

    note("engine: %d names" % len(c_list))
    note("oracle: %d names" % len(js_list))
    for n in sorted(c_list - js_list):
        fails.append("[4] %r is intrinsic in the engine but not in the oracle "
                     "-- the model would predict a refusal the engine does not "
                     "make" % n)
    for n in sorted(js_list - c_list):
        fails.append("[4] %r is intrinsic in the oracle but not in the engine "
                     "-- the model would predict an admission the engine "
                     "refuses" % n)

    # Date and Math are excluded by DECISION (user, 2026-09-12): clock and RNG
    # are the side channels M-SES left open. A checker is the only thing that
    # keeps a decision from being undone by a convenient edit.
    for n in ("Date", "Math", "Promise", "Symbol", "Proxy", "Reflect",
              "ArrayBuffer", "SharedArrayBuffer"):
        if n in c_list or n in js_list:
            fails.append("[4] %r was added to the intrinsics allowance; it is "
                         "excluded by decision (clock/RNG/scheduling/registry/"
                         "channel) -- if that is meant to change, change it "
                         "here and in VERIFICATION.md V3 as well" % n)


# ---------------------------------------------------------------------------
# 5. Denial codes: the C enumeration vs the V12 golden corpus.
#
# MANUAL §3.2 promises tenants that codes are stable across releases and tells
# them to pin CI to codes rather than message text. The corpus
# (t/tools/golden-denials.js) is what makes that checkable -- but only while it
# describes the same set the runtime has. A code added to the C enum without a
# corpus row would ship unfrozen: nothing would notice it changing later. A row
# left behind after a code is removed is the opposite rot, a frozen contract for
# something that no longer exists.
#
# A row may say `unreachable` instead of carrying a probe; that counts as
# covered, because the reason is recorded. What is not allowed is silence.
# ---------------------------------------------------------------------------
def check_denial_codes():
    print("[5] denial codes (C enum vs the V12 golden corpus)")
    cmp_c = read("src/js/ngx_js_compartment.c")
    m = re.search(r"ngx_js_denial_names\[NGX_JS_DENIAL_LAST\]\s*=\s*\{(.*?)\};",
                  cmp_c, re.S)
    if not m:
        fails.append("[5] ngx_js_denial_names[] not found")
        return
    # Strip C comments FIRST: the table carries explanatory prose, and a quoted
    # phrase inside a comment was being read as a code ("for the next N
    # seconds"), which then failed for want of a corpus row. The checker should
    # read the table, not the commentary around it.
    table = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    c_codes = set(re.findall(r'"([^"]+)"', table))
    note("C: %s" % sorted(c_codes))

    golden = read("t/tools/golden-denials.js")
    # the GOLDEN array only -- REFUSALS holds the admission half (check 6), and
    # scraping the whole file made every refusal code look like a denial row
    # that the runtime "no longer emits".
    gblock = golden[golden.index("var GOLDEN = ["):golden.index("var REFUSALS = [")]
    g_codes = set(re.findall(r"code:\s*'([^']+)'", gblock))
    note("corpus: %s" % sorted(g_codes))

    for c in sorted(c_codes - g_codes):
        fails.append("[5] denial code %r has no row in the golden corpus -- it "
                     "would ship unfrozen, and MANUAL §3.2 promises tenants it "
                     "is stable" % c)
    for g in sorted(g_codes - c_codes):
        fails.append("[5] the golden corpus freezes %r, which the runtime no "
                     "longer emits -- a contract for something that does not "
                     "exist" % g)


# ---------------------------------------------------------------------------
# 6. Refusal codes: the C enumeration vs the V12 golden corpus.
#
# The other half of MANUAL §3.2, added with [TBD-2]. A denial code names a gate
# that fired at RUN time; a refusal code names why a fragment was never admitted.
# Same rule as check [5], for the same reason: a code that ships without a row is
# an unfrozen promise, and a row without a code is a contract for something that
# does not exist.
#
# The NONE row is skipped deliberately -- it is the success value, spelled "",
# and is not a code any tenant can be refused with.
# ---------------------------------------------------------------------------
def check_refusal_codes():
    print("[6] refusal codes (C enum vs the V12 golden corpus)")
    cmp_c = read("src/js/ngx_js_compartment.c")
    m = re.search(r"ngx_js_refusal_codes\[NGX_JS_REFUSAL_LAST\]\s*=\s*\{(.*?)\};",
                  cmp_c, re.S)
    if not m:
        fails.append("[6] ngx_js_refusal_codes[] not found")
        return
    table = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    c_codes = set(c for c in re.findall(r'"([^"]*)"', table) if c)
    note("C: %s" % sorted(c_codes))

    golden = read("t/tools/golden-denials.js")
    # the REFUSALS array only -- GOLDEN's rows are the denial half (check 5)
    rblock = golden[golden.index("var REFUSALS = ["):]
    g_codes = set(re.findall(r"code:\s*'(E_[A-Z_]+)'", rblock))
    note("corpus: %s" % sorted(g_codes))

    for c in sorted(c_codes - g_codes):
        fails.append("[6] refusal code %r has no row in the golden corpus -- "
                     "MANUAL §3.2 tells tenants to pin CI to codes, so an "
                     "unprobed one ships unfrozen" % c)
    for g in sorted(g_codes - c_codes):
        fails.append("[6] the golden corpus freezes refusal %r, which the "
                     "runtime cannot emit" % g)


# ---------------------------------------------------------------------------
# 7. SPEC.md currency: the normative read must NAME what the code enumerates.
#
# M2.5 produced SPEC.md as "the clean normative read, current truth stated once".
# A document like that decays silently: it was accurate the day it was written,
# the code moved eleven times, and nothing ever failed. When this check was
# added, SPEC.md had ZERO mentions of `routes`, `ttl`, the refusal codes,
# `cap.expired` or the session registry -- all shipped -- and its status section
# was stamped v5.35 against a delta log at v5.75.
#
# So the spec's completeness claims are checked the way the corpora are: every
# member of a set the spec calls closed must appear in it.
#
# WHAT IS AND IS NOT CHECKED, deliberately. The mediation flavors, the denial
# codes and the ops-resource names are short, closed, and stated IN the spec, so
# they are checked by name. The refusal codes are NOT: fifteen strings would
# duplicate MANUAL §3.2, and a spec that copies a table acquires a second place
# for it to be wrong. For those the spec must name the RUNTIME enumerator
# instead, so a reader is sent to the one authority rather than a stale copy.
# Demanding the list here would trade one staleness for another.
# ---------------------------------------------------------------------------
def shipped_flavors(tag):
    """
    The mediation vocabulary that SHIPS, from the JS bootstrap's FLAVORS table.

    ONE extraction, read by checks [7] and [8].  Two copies of this regex would be
    the very drift this file exists to catch -- and [8] was written second, which
    is exactly when a second copy gets made.

    The bootstrap is C string literals, so a table long enough to wrap is split
    across two of them -- which is what happened when `allowHosts` was added, and
    the check reported "table not found".  It failed CLOSED, which is the right
    direction, but a checker that makes a source file unformattable is a checker
    people work around.  So the C string-concatenation seams are stitched shut
    before matching: `" ... "` followed by whitespace and another `"` is one JS
    string as far as the engine is concerned, and it should be one here too.
    """
    com_c = re.sub(r'"\s*\n\s*"', '', read("src/js/ngx_js_com.c"))
    m = re.search(r'var FLAVORS=\{([^"]*)\}', com_c)
    if not m:
        fails.append("%s the FLAVORS table was not found in ngx_js_com.c" % tag)
        return set()

    flavors = set(re.findall(r"([a-zA-Z]+):1", m.group(1)))
    note("flavors: %s" % sorted(flavors))
    return flavors


def check_spec_currency():
    print("[7] SPEC.md names what the code enumerates")
    spec = read("js_comcon/docs-v5.0/SPEC.md")

    # the bootstrap, with the C string-concatenation seams stitched shut (see
    # shipped_flavors(), which needs the same treatment for the same reason)
    com_c = re.sub(r'"\s*\n\s*"', '', read("src/js/ngx_js_com.c"))

    # (a) the mediation vocabulary, from the JS bootstrap's FLAVORS table.
    flavors = shipped_flavors("[7]")
    if flavors:
        for f in sorted(flavors):
            if ("`%s`" % f) not in spec:
                fails.append("[7] SPEC.md never names the mediation flavor %r, "
                             "so its 'closed vocabulary' claim is incomplete" % f)

    # (b) the denial codes, from the C names table (comments stripped, as in [5])
    cmp_c = read("src/js/ngx_js_compartment.c")
    m = re.search(r"ngx_js_denial_names\[NGX_JS_DENIAL_LAST\]\s*=\s*\{(.*?)\};",
                  cmp_c, re.S)
    if not m:
        fails.append("[7] ngx_js_denial_names[] not found")
    else:
        table = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
        codes = set(re.findall(r'"([^"]+)"', table))
        note("denial codes: %s" % sorted(codes))
        for c in sorted(codes):
            if c not in spec:
                fails.append("[7] SPEC.md never names the denial code %r -- the "
                             "spec states this axis as closed, so a code missing "
                             "from it is a promise the spec does not make" % c)

    # (c) the ops-resource names, from the std.ops OPS_RES table
    res = set(re.findall(r'"\s*([a-zA-Z]+):\{doc:', com_c))
    note("ops resources: %s" % sorted(res))
    if not res:
        fails.append("[7] the OPS_RES table was not found in ngx_js_com.c")
    # Matched as a BACKTICKED IDENTIFIER, never as a bare word. `log` and `mode`
    # are ordinary English words, so a bare-word search would pass on any prose
    # at all -- the check would be inert while looking green. Requiring the
    # backticked form also forces the spec to name the key an operator actually
    # passes rather than a paraphrase of it: this check's first run found
    # `bindings` described only as "binding/epoch store", which tells a reader
    # the concept and not the identifier they have to type.
    for r in sorted(res):
        if ("`%s`" % r) not in spec:
            fails.append("[7] SPEC.md never names the ops-resource `%s` -- §10 "
                         "calls this set closed, and an operator passes it by "
                         "identifier, not by paraphrase" % r)

    # (d) the refusal codes are delegated, not copied -- so the spec must name
    #     the enumerator that IS authoritative for them.
    if "comcon.refusalCodes()" not in spec:
        fails.append("[7] SPEC.md does not name comcon.refusalCodes() -- the "
                     "refusal codes are deliberately not listed there, so "
                     "without the enumerator a reader has no authority to "
                     "go to")


# ---------------------------------------------------------------------------
# [8] A "NOT BUILT" LIST MAY NOT NAME A WORD THAT SHIPPED
# ---------------------------------------------------------------------------

def check_absent_lists():
    """
    The rot this catches happened, and nothing caught it for three days: the
    ROADMAP's POSITION block -- the most-read paragraph in the doc set -- went on
    saying that the posture vocabulary and `allowHosts`/`ttl`/`window` "need
    C-side enforcement" and that `cosign` "needs an approval-recording protocol",
    after all five had shipped.

    Check [7] fails when SPEC.md is missing a word the code HAS.  This is the same
    rot from the other side: a list of what is ABSENT going on naming what is
    PRESENT.  Both are the V7 rule -- a hand-maintained list rots silently,
    because nothing fails when it stops matching the code.

    IT PARSES ONE CANONICAL LINE PER DOCUMENT, NOT PROSE.  The first version of
    this check scanned the whole "what is not built" region for backticked words
    and asked whether each line also said "shipped".  It found the real drift --
    and it also flagged a DATED bullet inside the POSITION block that correctly
    recorded "seven of ten words ship" as of v5.85.  That is history, and a
    checker that argues with history teaches people to disable it.  The block
    genuinely mixes current status with dated records in the same bullets, so no
    rule over that prose can be exact.

    So the documents were changed instead, which is the V7 move: each carries ONE
    machine-readable line, and the prose around it is free to say whatever it
    says because it is no longer the list.  That turns this from a heuristic into
    a derived comparison like checks [1]-[6].

    NOT CHECKED, and worth saying: the other direction.  Nothing here proves the
    canonical list is COMPLETE -- that every word of the intended vocabulary is
    either shipped or listed as absent -- because the intended ten live in a prose
    table in MANUAL.md and deriving them would re-introduce exactly the parsing
    this check just stopped doing.
    """
    print("[8] a NOT-BUILT list does not name a word that shipped")

    flavors = shipped_flavors("[8]")
    if not flavors:
        return

    docs = ["js_comcon/docs-v5.0/ROADMAP.md",
            "js_comcon/docs-v5.0/INCREMENT_MLIB.md"]

    for rel in docs:
        text = read(rel)

        # The line, plus its continuations: these documents wrap near 100 columns,
        # so the canonical list may spill onto following lines and all of them are
        # read -- but it STOPS at a blank line.  The first version used one regex
        # with re.S and swallowed the paragraph after it, which mentions every
        # shipped word, so the check reported six drifts that were not there.  A
        # span that grows silently is worse than no span.
        lines = text.splitlines()
        span, seen = None, False
        for line in lines:
            if not seen:
                m = re.search(r"NOT BUILT \(canonical list[^)]*\):\*\*(.*)$", line)
                if m:
                    seen, span = True, m.group(1)
                continue
            # A continuation ends at a blank line, a new bullet, a heading, or a
            # new emphasised paragraph.  "Blank line" alone is not enough: in the
            # ROADMAP the list sits inside a blockquote whose next line is the
            # next BULLET, so the first version ran on to the end of the document
            # and reported six drifts from prose it should never have read.
            body = line.lstrip("> ").strip()
            if body == "" or body.startswith(("- ", "#", "**", "| ")):
                break
            span += " " + body

        if not seen:
            fails.append("[8] %s has no 'NOT BUILT (canonical list ...)' line -- "
                         "this check must not pass by failing to find its subject"
                         % os.path.basename(rel))
            continue
        named = set(w.rstrip(".*") for w in re.findall(r"`([^`]+)`", span))
        note("%s absent-list: %s" % (os.path.basename(rel), sorted(named)))

        for w in sorted(named):
            if w in flavors:
                fails.append("[8] %s's canonical NOT-BUILT list names `%s`, which "
                             "the code SHIPS (it is in the FLAVORS table) -- a "
                             "list of what is absent is naming what is present"
                             % (os.path.basename(rel), w))


check_p_symbols()
check_portals()
check_ops_resources()
check_intrinsics()
check_denial_codes()
check_refusal_codes()
check_spec_currency()
check_absent_lists()

print("")
if fails:
    print("DRIFT (%d):" % len(fails))
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("all eight enumeration checks agree with the code")
sys.exit(0)
