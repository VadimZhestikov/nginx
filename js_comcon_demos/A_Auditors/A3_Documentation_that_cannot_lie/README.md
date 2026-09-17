# A3 — Documentation that cannot lie

**Audience:** auditors and tenant-facing teams who need "your available API"
to be true on the day it is read.

**In one sentence:** the tenant's manual is rendered from the contract the
binding holds, through the same grant translation the kernel enforces, so
what is documented is what works and nothing else.

## Run

```bash
bash test.sh                       # 12 checks
curl http://127.0.0.1:8218/docs
curl http://127.0.0.1:8218/model
curl http://127.0.0.1:8218/narrow      # a new epoch; the manual says so
```

## What you see

```markdown
# acme -- your available API
derived from your contract, epoch 0; posture: inherit; profile: restrictive

## Capabilities
- `s` -- socket: address (read), port (read)
  - budget: 100 per 60 s (key acme:s)
- `http` -- server facet within `/acme/*`: paths(), route, allowed(path)
- `out` -- outbound within `https://*.example.com`: request(url)
  - open on day mask 62, 540-1020 (minutes, UTC)
- `author` -- author: include(source, contract)
  - may hold 4 sub-fragments at once

## Names you may mention
- imports: JSON, s, http, out, author; language intrinsics: allowed

## Bounds
- deadline: 250 ms per invocation (an abort is not catchable)
- allocation: default (16 MB) bytes per invocation
- retained: 1048576 bytes across invocations

## Admission
- request fields: checked against the sealed schema
- tests: run in the compartment before admission
- identity: not pinned
```

## Why it cannot lie

`comcon.std.docs.model(name, contract | fragment | handle)` reads the contract
a binding carries (`.contract`, a frozen copy attached by `include` and
`bindAt`) and translates each grant with the function `include` itself feeds
to the C side. There is no second table to drift: a redacted field is absent
from the manual because it is absent from the capability. `render()` is the
same model as Markdown; `ops.docs(name)` renders a registered binding with its
live epoch, so a replaced epoch changes the manual on the next query.

## Where to read more

- SHOWCASE §29 (`REAL CODE`); `SHOWCASE-gaps.md` G-16 (closed v5.127).
- Tests: `t/comcon_std_docs.t`, `t/comcon_std_lib.t` (`std.describe()`, the library's own honesty table).
