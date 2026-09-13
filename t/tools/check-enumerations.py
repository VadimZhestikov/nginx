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

Exit 0 = the five enumerations agree with the code. Exit 1 = drift, printed.
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
    c_codes = set(re.findall(r'"([^"]+)"', m.group(1)))
    note("C: %s" % sorted(c_codes))

    golden = read("t/tools/golden-denials.js")
    g_codes = set(re.findall(r"code:\s*'([^']+)'", golden))
    note("corpus: %s" % sorted(g_codes))

    for c in sorted(c_codes - g_codes):
        fails.append("[5] denial code %r has no row in the golden corpus -- it "
                     "would ship unfrozen, and MANUAL §3.2 promises tenants it "
                     "is stable" % c)
    for g in sorted(g_codes - c_codes):
        fails.append("[5] the golden corpus freezes %r, which the runtime no "
                     "longer emits -- a contract for something that does not "
                     "exist" % g)


check_p_symbols()
check_portals()
check_ops_resources()
check_intrinsics()
check_denial_codes()

print("")
if fails:
    print("DRIFT (%d):" % len(fails))
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("all five enumerations agree with the code")
sys.exit(0)
