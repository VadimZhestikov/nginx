# P1 — A confined tenant handler

**Audience:** platform and SaaS teams who want to run a customer's request
logic inside nginx without trusting it.

**In one sentence:** the tenant's JavaScript is compiled at config load inside
a compartment that holds nothing, serves live requests through the ordinary
`location.handler`, and cannot name a single host object.

## Run

```bash
bash test.sh                       # 9 checks
# by hand:
../../../objs/nginx -p . -c nginx.conf
curl -D- -A me/1.0 -H 'X-Count: 41' http://127.0.0.1:8200/t
curl http://127.0.0.1:8200/denials
../../../objs/nginx -p . -c nginx.conf -s stop
```

## What you see

```
HTTP/1.1 200 OK
X-Tenant: acme
X-Tenant-Caged: yes            <- computed INSIDE the request: typeof nginx === "undefined"
X-Tenant-Seen-UA: me/1.0       <- request data crossed in as a copy
tenant handled GET /t caged=yes count=42
```

and **no** `X-Evil` header, although `tenant.js` returned one hidden behind a
CRLF: the boundary drops a header value it cannot serialise safely.

## How it works

| Piece | Role |
|---|---|
| `tenant.js` | the untrusted code, a real JS file; the host reads it as text |
| `host.js` | `comcon.include(String(tenant), { imports: ["nginx"] })` at config load, then `loc.handler = ...` |
| `nginx.conf` | only `js_source host.js;` — there is no tenant directive |

`imports` is the **admission contract**: the only free names the fragment may
mention. A tenant that mentions a name not listed is refused when nginx loads
its config (`E_ADMIT_FREENAME`), not at 3 a.m. on request 40 000. This host
lists `nginx` so the tenant may *probe* it, and the probe reads `undefined`:
listing a name lets the text mention it, while what a name *holds* comes only
from the contract's `grants` — and this contract grants nothing. Two walls,
both yours: admission decides what may be named, the environment decides what
a name is. Data in, data out.

## Where to read more

- Operator API: `js_comcon/docs-v5.0/OPERATOR_API.md` §3 (`include`).
- The tests behind this shape: `t/comcon_operator_handler.t`, `t/comcon_include_headers.t`.
- Next demo: **P2** shows how to observe a tenant's reaches before you enforce.
