# S4 — Sessions: an authenticated principal becomes an environment

**Audience:** security teams wiring identity (mTLS, OIDC, a peer credential)
to what a caller's code may hold, with revocation that needs no token chase.

**In one sentence:** you authenticate, `std.sessions` maps the principal to a
narrowing of an environment you hold, and a fragment bound under it holds
exactly that and nothing else.

## Run

```bash
bash test.sh                       # 8 checks
curl 'http://127.0.0.1:8207/session?who=ci@acme'
curl 'http://127.0.0.1:8207/session?who=dev@acme'
curl 'http://127.0.0.1:8207/session?who=greedy@acme'
curl 'http://127.0.0.1:8207/revoke?who=ci@acme'
```

## What you see

| principal | mapping | resolved | the probe holds |
|---|---|---|---|
| `ci@acme` | `{imports: ['s','JSON']}` | `granted: ["JSON","s"]` | the socket wrapper |
| `dev@acme` | `{imports: ['JSON']}` | `granted: ["JSON"]` | no socket |
| `nobody@nowhere` | none | `granted: []` + a reason | nothing |
| `greedy@acme` | `{imports: ['JSON','nginx']}` | **refused** | (a mapping that names what the env does not hold is an error, not a smaller session) |
| `ci@acme` after `/revoke` | row removed | `granted: []` | nothing |

## Three rules

1. **You authenticate; COMCON maps.** The `who` in this demo is a query
   parameter so you can drive it by hand. In production it must be something
   you verified. Passing a client-supplied string hands the client the
   session, and nothing in the platform can tell the difference.
2. **A mapping can only narrow, and it narrows *your* env.** `resolve(principal,
   env)` returns `env ∩ descriptor`. If the descriptor names something the env
   does not grant, `resolve` throws: a mapping that silently grants less than
   it says is one you cannot audit.
3. **Unknown, revoked and expired are the same answer:** an empty environment.
   Not an error to catch, not a default role.

A descriptor is **data** (a capability written into it is refused), stored in
`nginx.shared` so every worker resolves the same thing, optionally leased
(`ttl`). `describe()` reports `authenticates: false` in the surface itself.

## Where to read more

- `OPERATOR_API.md` §8a; `FOUNDATION.md` §8b (the design).
- Tests: `t/comcon_std_sessions.t`.
- Pair with **S1**'s `ttl`: a lease on the mapping expires the mapping; a
  `ttl` on the capability expires authority already handed out.
