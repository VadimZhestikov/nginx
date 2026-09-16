# O1 — A tenant proposes configuration; the operator reviews, applies, rolls back

**Audience:** operators and SREs who want self-service configuration for
tenants without handing them the config tree.

**In one sentence:** a proposal is inert text over the tenant's own subtree;
the operator typechecks it against a policy, sees the diff, applies it
all-or-nothing with explicit confirmation of guarded classes, and can roll it
back by hash.

## Run

```bash
bash test.sh                       # 16 checks
for op in review diff apply-noconfirm apply rollback atomic realize; do
    curl -s "http://127.0.0.1:8208/ctl?op=$op"; echo
done
```

## What you see

```
review            {"ok":true,"hash":"…","ops":["acme.root = allow (safe)", …],"refused":[],"now":{"root":"./html",…}}
diff              {"diff":["acme.root: \"./html\" -> \"/srv/acme\"", …],"now":{"root":"./html",…}}   nothing applied
apply-noconfirm   {"out":"needs-confirm"}                                                        proxy.pass is guarded
apply             {"applied":[…],"hash":"…","now":{"root":"/srv/acme","connectTimeout":2500,…}}
rollback          {"restored":…,"now":{"root":"./html",…}}
atomic            {"reviewOk":true,"out":"refused: …no_such_upstream…","now":{"root":"./html",…}}  first op undone
realize           {"quoted":"object, frozen","realized":42,"hidden":"refused: …"}
```

## The two quotation shapes

- **A config proposal** (`comcon.std.config`): config-shaped sentences over a
  subtree (`acme.root(...)`, `acme.proxy.pass(...)`). The policy says which
  subtree (`root`), which paths (`allow`), and which safety classes may be
  applied unasked (`allowClass`). A guarded class needs `confirm: [path]` at
  apply time. `review` is pure; the same text yields the same hash.
- **A function proposal** (`comcon.quote` / `comcon.realize`): a cap-free
  description the operator gives force to under an environment of the
  operator's choosing, restricted to the quotation's declared manifest.
  A quotation that names a free name outside its `imports` is refused at
  realization, even if the realizer's session holds it (the confused-deputy
  fix).

Both are **propose, don't hold**: the tenant never touches a capability; the
operator's authority is what applies the change.

## Where to read more

- `OPERATOR_API.md` §3 (config-proposal sibling), the `realize`/`quote` banner.
- Tests: `t/comcon_config_instance.t`, `t/comcon_realize.t`.
