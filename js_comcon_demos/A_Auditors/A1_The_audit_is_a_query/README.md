# A1 — The audit is a query, not an interview

**Audience:** auditors, security reviewers, procurement: the people who ask
"who can touch what, where is that library used, and can you stop this call?"
and want an answer they can re-run.

**In one sentence:** the trust report is a library verb over the resources a
session was handed, "where is `fetch` used" is a bytecode query with line
numbers, and a call no grant can name is rewritten by a reviewable
transformation installed as an epoch.

## Run

```bash
bash test.sh                       # 17 checks
curl http://127.0.0.1:8215/report
curl 'http://127.0.0.1:8215/where?name=fetch'
curl http://127.0.0.1:8215/v ; curl 'http://127.0.0.1:8215/rewrite?op=harden' ; curl http://127.0.0.1:8215/v
```

## What you see

```json
/report   {"verbs":[…18 verbs…],"bareVerbs":["describe"],
           "bindings":[{"name":"vendor","epoch":0,"tombstoned":false,"snapshots":1}],
           "trust":{"bindings":[…],"enforcedBy":[{"field":"imports","by":"C3 free-name check",…},…]},
           "denials":{"mode":"enforce","total":1,"byOp":{"sock.listener":1,…}},
           "noHost":["provenance","signing"], "absent":["std.postures.*",…]}
/where    {"references":2,"callsites":[{"line":9,"method":false},{"line":10,"method":false}],
           "functions":["real"],"readsAreQuotations":true,"bindingRedacted":true}
/v        AB|RAN:a,RAN:b        -> harden (2 sites) ->   XX|        -> rollback ->   AB|RAN:a,RAN:b
```

## Three things an auditor can re-run

1. **The trust report** (`comcon.std.ops(...).trustReport()`): a session takes
   its resources as arguments and reaches for nothing ambient, so "what can
   this session do" is `Object.keys(session)`. A session given nothing has only
   `describe()`. The two resources with no host spelling yet (`provenance`,
   `signing`) are listed with `host: null`, and the verbs they would enable are
   reported as withheld with a reason: a gap that is checkable, not invisible.
2. **The query** (`comcon.pom(fn).callsites("fetch")`): enumerated from
   bytecode, with lines, without running anything; a nested argument
   (`fetch(helper(2))`) is still attributed to `fetch`. Reads of the tree are
   quotations, never raw source, and the binding is redacted by default.
3. **The rewrite** (`ops.rewrite(name, "call(g)", wrapper)`): a *free* name is
   governed by a grant (no parser, nothing to evade); a *locally bound* alias
   is the residual no grant can name, and this reaches it. `$$` stands for the
   site's own source; the result is a quotation, reviewed, then installed as
   the next epoch and rolled back like any other.

## Where to read more

- SHOWCASE §37, §38, §25 (`REAL CODE` blocks); `SHOWCASE-gaps.md` G-13, G-16, G-20.
- Tests: `t/comcon_std_ops.t`, `t/comcon_pom_callsites.t`, `t/comcon_pom_nodeview.t`, `t/comcon_pom_harden.t`.
