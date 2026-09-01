# COMCON dogfood — increment A acceptance

A **confined tenant** serving **real HTTP traffic** across multiple workers,
caged by the environment it was handed rather than by trust. This is the
acceptance demo for COMCON increment A: everything the increment built,
exercised end to end on live requests.

## Run

```bash
./test.sh          # 12 checks, starts nginx, asserts, stops
# or interactively:
../../objs/nginx -p . -c nginx.conf     # add `daemon off;` to watch it
curl -D- -A me/1.0 http://127.0.0.1:8119/mirror
curl http://127.0.0.1:8119/denials
```

## What it demonstrates

- **A confined tenant does real work.** `tenant.js` is a mirror-style policy —
  count requests, echo a request header, tag the response — the exact "count +
  tag" shape of the M1 spike, but written by an untrusted party.
- **Deny-by-default (the primary control).** The tenant's whole world is the
  names it was granted (`report`, `onRequest`, the host's `granted` socket).
  There is no `nginx`, no `createSocket`, no way out. Each response reports
  `caged=yes`, computed *inside the request* from `typeof nginx === "undefined"`.
- **The A1 reach gate isolates on live traffic.** The host **grants** the tenant
  a real listener socket. The tenant holds it and can read its scalar fields,
  but `granted.listener` — the entry to the `sock→listener→server→addLocation`
  reach cycle — returns `null` cross-compartment. Every such call is an
  enforce-mode **denial**.
- **Data in, data out (A3.1).** Request headers arrive as a plain `req.headers`
  object (a copy, no capability); response headers leave via the return value's
  `.headers`. A tenant value containing CRLF (`"a\r\nX-Evil: 1"`) is **dropped**,
  not smuggled — the header-injection surface is closed.
- **The host closes the audit→enforce loop.** `/denials` (a host-owned location)
  serves `nginx.tenantDenials()` — `{mode, total, byOp}` — so an operator sees
  the tenant's reach attempts. Flip `js_tenant_mode audit;` in `nginx.conf` to
  watch the same reach be logged-and-*allowed* instead (the onboarding mode).

## Files

| File | Role |
|---|---|
| `host.js` | HOST_ROOT: creates + grants the socket, wires `/denials` |
| `tenant.js` | the confined mirror policy (compartment 1) |
| `nginx.conf` | 2 workers; `/mirror` → tenant, `/denials` → host |
| `test.sh` | 12 acceptance checks |

## Honest edges (what this is *not* yet)

- **Counting is per-worker.** Cross-worker shared state (a granted counter
  capability) is a later capability-grant, beyond increment A's socket grant;
  the zero-capability request path counts locally.
- **One tenant.** Multi-tenant (N named compartments) is post-increment-A.
- **No budgets.** CPU/gas metering of the tenant runtime is milestone S5.

The mechanisms here are covered by CI: `t/comcon_tenant_headers.t`,
`comcon_tenant_request.t`, `comcon_tenant_deny.t`, `comcon_tenant_grant.t`,
`comcon_denial_log.t`, `comcon_audit_mode.t`.
