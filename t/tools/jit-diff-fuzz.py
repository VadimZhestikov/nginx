#!/usr/bin/env python3
"""
jit-diff-fuzz.py -- differential fuzz of the compiled tier against the
interpreter, with a delta reducer.  The instrument that found F20.

  python3 t/tools/jit-diff-fuzz.py run [--seeds A-B] [--n N]
      Generate N small functions per seed, run each program twice (plain
      qjs = interpreter; qjs --jit-compile-all = every function compiled),
      and print the functions whose output differs.  Exit 1 on any
      divergence, on a compiled-run crash, or on a DEAD seed (a program that
      did not print one line per function -- a probe that cannot fail is
      not a probe).

  python3 t/tools/jit-diff-fuzz.py reduce SEED IDX [--n N]
      Delta-reduce function IDX of SEED to a minimal diverging function and
      print it with both tiers' output.  Reduction is greedy: drop
      statements, unwrap if/for, hoist a child over its parent expression,
      replace a subtree by 0 / 1 / 1.5 / a variable -- each step kept only
      if the divergence survives.

WHAT IT GENERATES: functions over three locals initialised from a small
literal set (ints, doubles, int32 edges, NaN), with assignments, compound
assignments, if/else, three-iteration for loops, pushes, and expressions
over + - * & | ^ << >> >>> < > <= >= === !== ?: unary + ~ -.  That is the
population the typed lowering (INT / NUMBER locals and stack slots) acts
on; string and object shapes are covered by the nginx differential
(t/comcon_include_faithfulness.t), not here.

THE ORACLE is the interpreter of the same binary.  A divergence is a
miscompile by definition (SR-2: compiled == interpreted); which tier is
right is decided by the spec afterwards.

Requires quickjs/qjs built with CONFIG_JIT=y (make -C quickjs CONFIG_JIT=y qjs).
Runs under $JIT_FUZZ_TMP (default /tmp/jit-diff-fuzz-<pid>); each compiled
run gets a fresh QJS_JIT_CACHE so a stale .so can never answer for the
current codegen.
"""
import os, random, shutil, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
QJS = os.path.join(ROOT, "quickjs", "qjs")
T = os.environ.get("JIT_FUZZ_TMP") or "/tmp/jit-diff-fuzz-%d" % os.getpid()

LITS = ["0", "1", "2", "3", "7", "-1", "1.5", "2.5", "0.25", "-0.5", "1e10",
        "2147483647", "-2147483648", "4294967296", "NaN"]
BINOPS = ["+", "-", "*", "&", "|", "^", "<<", ">>", ">>>", "<", ">", "<=", ">=", "===", "!=="]
ASSOPS = ["+=", "-=", "*=", "&=", "|=", "^=", "<<=", ">>="]


# ---- generator (AST as nested tuples/lists so the reducer can rewrite it) ----

def lit():
    return ("lit", random.choice(LITS))

def expr(vs, d=0):
    if d > 2 or random.random() < 0.3:
        return ("var", random.choice(vs)) if random.random() < 0.6 else lit()
    k = random.random()
    a = expr(vs, d + 1)
    b = expr(vs, d + 1)
    if k < 0.5: return ("bin", random.choice(BINOPS), a, b)
    if k < 0.6: return ("tern", a, b, expr(vs, d + 1))
    if k < 0.7: return ("bin", "|", a, ("lit", "0"))
    if k < 0.8: return ("un", "+", a)
    if k < 0.9: return ("un", "~", a)
    return ("un", "-", a)

def stmt(vs, d=0):
    k = random.random()
    v = random.choice(vs)
    if k < 0.35: return ("assign", v, expr(vs))
    if k < 0.5: return ("opassign", v, random.choice(ASSOPS), expr(vs))
    if d < 2 and k < 0.7: return ("if", expr(vs), [stmt(vs, d + 1)], [stmt(vs, d + 1)])
    if d < 2 and k < 0.85: return ("for", "k%d" % d, [stmt(vs, d + 1)])
    return ("push", expr(vs))

def gen_func():
    vs = ["a", "b", "c"]
    return {"init": [lit(), lit(), lit()],
            "body": [stmt(vs) for _ in range(random.randint(2, 6))]}

def gen_seed(seed, n):
    random.seed(seed)
    return [gen_func() for _ in range(n)]


# ---- serialisation ----

def se(e):
    t = e[0]
    if t in ("lit", "var"): return e[1]
    if t == "bin": return "(%s %s %s)" % (se(e[2]), e[1], se(e[3]))
    if t == "tern": return "(%s ? %s : %s)" % (se(e[1]), se(e[2]), se(e[3]))
    if t == "un": return "(%s %s)" % (e[1], se(e[2]))   # space: "(- -1)" not "(--1)"
    raise ValueError(t)

def ss(s):
    t = s[0]
    if t == "assign": return "%s = %s;" % (s[1], se(s[2]))
    if t == "opassign": return "%s %s %s;" % (s[1], s[2], se(s[3]))
    if t == "if":
        return "if (%s) { %s } else { %s }" % (se(s[1]), " ".join(map(ss, s[2])), " ".join(map(ss, s[3])))
    if t == "for":
        return "for (var %s = 0; %s < 3; %s++) { %s }" % (s[1], s[1], s[1], " ".join(map(ss, s[2])))
    if t == "push": return "o.push(%s);" % se(s[1])
    raise ValueError(t)

def sf(name, f):
    return ("function %s(){ var o=[]; var a = %s, b = %s, c = %s; %s o.push(a,b,c); return o.join(','); }"
            % (name, se(f["init"][0]), se(f["init"][1]), se(f["init"][2]), " ".join(map(ss, f["body"]))))

def program(funcs):
    out = [sf("f%d" % i, f) for i, f in enumerate(funcs)]
    out += ["try { print('f%d', f%d()); } catch(e) { print('f%d', 'throw', e.name); }" % (i, i, i)
            for i in range(len(funcs))]
    return "\n".join(out) + "\n"


# ---- running both tiers ----

def run_both(src, tag):
    os.makedirs(T, exist_ok=True)
    p = os.path.join(T, "prog_%s.js" % tag)
    with open(p, "w") as fh:
        fh.write(src)
    r1 = subprocess.run([QJS, p], capture_output=True, text=True, timeout=300)
    cache = os.path.join(T, "cache_%s" % tag)
    shutil.rmtree(cache, ignore_errors=True)
    os.makedirs(cache)
    env = dict(os.environ, QJS_JIT_CACHE=cache)
    r2 = subprocess.run([QJS, "--jit-compile-all", p], capture_output=True, text=True, timeout=900, env=env)
    a = r1.stdout + r1.stderr
    b = r2.stdout + r2.stderr
    if r2.returncode not in (0, 1):
        b += "\n[compiled run exited %d]" % r2.returncode
    return a, b

def differs(f):
    a, b = run_both(program([f]), "red")
    return a != b


# ---- reducer ----

def replace(root, path, val):
    if not path: return val
    k = path[0]
    if isinstance(root, dict):
        r = dict(root); r[k] = replace(root[k], path[1:], val); return r
    if isinstance(root, list):
        r = list(root); r[k] = replace(root[k], path[1:], val); return r
    r = list(root); r[k] = replace(root[k], path[1:], val); return tuple(r)

def subtrees_expr(e, path):
    yield path, e
    t = e[0]
    if t == "bin":
        yield from subtrees_expr(e[2], path + [2]); yield from subtrees_expr(e[3], path + [3])
    elif t == "tern":
        for i in (1, 2, 3): yield from subtrees_expr(e[i], path + [i])
    elif t == "un":
        yield from subtrees_expr(e[2], path + [2])

def expr_paths(f):
    out = []
    for i, e in enumerate(f["init"]):
        out += list(subtrees_expr(e, ["init", i]))
    def walk(stmts, path):
        for i, s in enumerate(stmts):
            p = path + [i]
            t = s[0]
            if t == "assign": out.extend(subtrees_expr(s[2], p + [2]))
            elif t == "opassign": out.extend(subtrees_expr(s[3], p + [3]))
            elif t == "push": out.extend(subtrees_expr(s[1], p + [1]))
            elif t == "if":
                out.extend(subtrees_expr(s[1], p + [1])); walk(s[2], p + [2]); walk(s[3], p + [3])
            elif t == "for": walk(s[2], p + [2])
    walk(f["body"], ["body"])
    return out

def stmt_lists(f):
    out = [(["body"], f["body"])]
    def walk(stmts, path):
        for i, s in enumerate(stmts):
            if s[0] == "if":
                out.append((path + [i, 2], s[2])); walk(s[2], path + [i, 2])
                out.append((path + [i, 3], s[3])); walk(s[3], path + [i, 3])
            elif s[0] == "for":
                out.append((path + [i, 2], s[2])); walk(s[2], path + [i, 2])
    walk(f["body"], ["body"])
    return out

def reduce_func(f):
    if not differs(f):
        raise SystemExit("the function does not diverge to begin with")
    changed = True
    while changed:
        changed = False
        for path, stmts in stmt_lists(f):
            for i in range(len(stmts)):
                cands = [stmts[:i] + stmts[i + 1:]]
                s = stmts[i]
                if s[0] == "if":
                    cands += [stmts[:i] + list(s[2]) + stmts[i + 1:],
                              stmts[:i] + list(s[3]) + stmts[i + 1:],
                              stmts[:i] + [("push", s[1])] + list(s[2]) + stmts[i + 1:]]
                if s[0] == "for":
                    cands.append(stmts[:i] + list(s[2]) + stmts[i + 1:])
                for c in cands:
                    cand = replace(f, path, c)
                    if differs(cand):
                        f = cand; changed = True; break
                if changed: break
            if changed: break
        if changed: continue
        for path, e in expr_paths(f):
            if e[0] == "var": continue
            if e[0] == "lit":
                cands = [("lit", l) for l in ("0", "1") if e[1] != l]
            else:
                kids = [e[2], e[3]] if e[0] == "bin" else [e[1], e[2], e[3]] if e[0] == "tern" else [e[2]]
                cands = kids + [("lit", l) for l in ("0", "1", "1.5")] + [("var", v) for v in ("a", "b", "c")]
            for c in cands:
                cand = replace(f, path, c)
                if differs(cand):
                    f = cand; changed = True; break
            if changed: break
    return f


# ---- CLI ----

def parse_args(argv):
    opts = {"seeds": "1-12", "n": 60}
    pos = []
    i = 0
    while i < len(argv):
        if argv[i] == "--seeds": opts["seeds"] = argv[i + 1]; i += 2
        elif argv[i] == "--n": opts["n"] = int(argv[i + 1]); i += 2
        else: pos.append(argv[i]); i += 1
    return opts, pos

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("run", "reduce"):
        print(__doc__); return 2
    if not os.access(QJS, os.X_OK):
        print("no %s (make -C quickjs CONFIG_JIT=y qjs)" % QJS); return 2
    opts, pos = parse_args(sys.argv[2:])
    n = opts["n"]
    if sys.argv[1] == "reduce":
        seed, idx = int(pos[0]), int(pos[1])
        r = reduce_func(gen_seed(seed, n)[idx])
        print(sf("reduced", r))
        a, b = run_both(program([r]), "red")
        print("  interpreter:", a.strip())
        print("  compiled   :", b.strip())
        return 0
    lo, hi = (opts["seeds"].split("-") + [None])[:2]
    lo = int(lo); hi = int(hi) if hi else lo
    bad = 0
    for seed in range(lo, hi + 1):
        funcs = gen_seed(seed, n)
        a, b = run_both(program(funcs), "seed%d" % seed)
        la, lb = a.splitlines(), b.splitlines()
        if len(la) != n:
            print("DEAD seed %d: interpreter printed %d lines, not %d: %s" % (seed, len(la), n, la[:1]))
            bad += 1; continue
        div = [i for i in range(n) if i >= len(lb) or la[i] != lb[i]]
        if div:
            bad += 1
            print("seed %d: %d diverging: %s" % (seed, len(div), div[:20]))
            for i in div[:3]:
                print("  f%d  interpreter: %s" % (i, la[i]))
                print("  f%d  compiled   : %s" % (i, lb[i] if i < len(lb) else "<missing>"))
            if len(lb) < len(la):
                print("  compiled run ended early:", b.strip().splitlines()[-1:])
        else:
            print("seed %d: %d functions, compiled == interpreted" % (seed, n))
    print("jit-diff-fuzz: %s" % ("PASS" if bad == 0 else "FAIL (%d seed(s))" % bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
