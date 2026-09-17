# L3 — Pinned by hash: the silent update that wasn't

**Audience:** release engineering and supply-chain security: the people who
review a file on Friday and need Saturday's "patch" to the same path to be a
loud admission failure, not Monday's incident.

**In one sentence:** a fragment is pinned by `identity` and a dependency by
`sha256`; the pins are computed from the reviewed bytes by any outside tool,
and a file that no longer hashes the same is refused before it exists as a
callable.

## Run

```bash
bash test.sh                       # 8 checks
SRC=$(curl -s http://127.0.0.1:8214/source)
PIN=$(printf '%s' "$SRC" | perl -MDigest::SHA=sha256,sha256_hex -e 'local $/; print sha256_hex(sha256(<STDIN>)."c2-tenant-env-1")')
curl "http://127.0.0.1:8214/pin?identity=$PIN&drift=0"     # admitted
curl "http://127.0.0.1:8214/pin?identity=$PIN&drift=1"     # refused: one byte moved
SHA=$(sha256sum vendor/magic-utils.js | cut -d' ' -f1)
curl "http://127.0.0.1:8214/dep?sha256=$SHA&path=$PWD/vendor/magic-utils.js"
```

## What you see

```json
reviewed text, reviewed pin   {"admitted":true,"result":{"ok":true,"total":750,"note":"reviewed"}}
one byte moved, same pin      {"admitted":false,"code":"…","why":"… identity …"}
the library, its hash         {"admitted":true,"result":"1.4.2 hello-world 2"}
the library patched           {"admitted":false,"why":"… hash mismatch …"}
```

## How it works

- **`identity`** is `sha256_hex(sha256(source) ‖ "c2-tenant-env-1")`: the hash of the
  text, tagged with the schema the fragment is admitted against. A reviewer
  computes it once from the bytes they read; the include refuses anything else.
- **`deps: [{name, path, sha256}]`** reads the file, verifies the hash, evaluates it
  as a bare pure script (its completion value is the library object) and binds
  it as a per-fragment closure parameter. Nothing lands on a shared global, so
  two fragments pinning two versions of the same library coexist.
- Both refusals happen at admission. With a live binding (**L1**) the previous
  epoch keeps serving while the unreviewed text is refused.

The demo takes the hashes from the query string so the test can compute them
with `perl` and `sha256sum` from the same bytes: what is asserted is that the
tree and an outside tool agree on what was reviewed.

## Where to read more

- SHOWCASE §2 and §40 (`REAL CODE`); `SHOWCASE-gaps.md` G-01 for the revocation half.
- Tests: `t/comcon_include_admit.t` (identity), `t/comcon_include_deps.t` (deps).
