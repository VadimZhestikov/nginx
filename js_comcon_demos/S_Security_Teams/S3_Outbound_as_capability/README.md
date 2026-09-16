# S3 — Outbound reach as a capability

**Audience:** security teams who must know exactly which destinations a piece
of tenant code can talk to, and want the answer enforced where the code runs.

**In one sentence:** a fragment gets no `fetch`; it gets an outbound
capability narrowed by a destination glob, records an intent through it, and
the host performs what the glob let through.

## Run

```bash
bash test.sh                       # 9 checks
curl http://127.0.0.1:8206/outbound
```

## What you see

```json
{"tenant":{"inGlob":"recorded",      <- https://api.example.com/... recorded
           "offGlob":"denied",       <- https://evil.net/... denied (out.host)
           "plainHttp":"denied",     <- http:// is not https://: the scheme is matched exactly
           "creds":"refused",        <- https://api.example.com@evil.net is refused, not parsed around
           "drain":"denied"},        <- pending()/clear() are the host's half (out.drain)
 "hostQueue":{"requests":[{"url":"https://api.example.com/v1/orders", ...}],"dropped":0},
 "denials":{"out.host":2,"out.drain":1}}
```

## Why not a fetch

A confined fragment is invoked synchronously: `JS_Call`, then the result is
serialised. A capability that performed I/O could not be handed to it without
making invocation asynchronous, which would touch the deadline and the memory
allowance on the most safety-critical path there is. So the capability
**records intent** and the host performs it, exactly as `comcon.std.config`
does for configuration: the tenant proposes what it cannot apply. In
production the host calls `comcon.std.outbound.perform(cap, req)`, which
reads the queue, awaits the I/O and clears exactly what it performed.

- **The glob is checked in the compartment**, at the moment of the request.
  That is what makes it an attenuation of authority rather than a filter over
  data afterwards.
- **Host globs wildcard on the left** (`*.example.com`); a scheme, if written,
  is matched exactly. Two different globs are refused at `mediate()` rather
  than merged.
- **The queue is bounded** (32 intents); past that a request is dropped and
  counted, never silently lost.
- Composes with `uses` (at most N an hour) and `ttl` (for the next hour).

## Where to read more

- `OPERATOR_API.md` §8e.
- Tests: `t/comcon_outbound.t`, `t/comcon_outbound_roundtrip.t`.
