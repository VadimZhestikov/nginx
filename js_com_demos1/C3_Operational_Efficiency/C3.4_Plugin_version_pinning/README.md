# C3.4 — Plugin Version Pinning

## What it shows

`nginx.use(path, config)` loads a plugin file and passes an arbitrary
configuration object to its `install()` function.  This demo maintains a
**plugin registry** — a JS array tracking which plugin versions are loaded,
when, and with what configuration.

At startup: `plugin_auth_v1.js` is loaded (API-key-only auth).
At runtime: `GET /admin/load-plugin/?name=acmecorp/auth&version=2.0.0`
hot-upgrades to `plugin_auth_v2.js` (adds Bearer token support) with the
same pinned key/token configuration — no nginx reload.

`GET /plugins/` returns the registry as JSON, showing name, version,
file path, load timestamp, and config for every loaded plugin.

## Why classic NGINX cannot do this

Classic NGINX modules are compiled in at build time or loaded as `.so` DSOs
at startup.  There is no mechanism to swap module behaviour at runtime, pin
module versions declaratively, or pass structured configuration to a module
from an admin endpoint.  The JS plugin system gives you all three.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Server on port 8171 |
| `handler.js` | Plugin registry + startup load + hot-swap endpoint |
| `plugin_auth_v1/index.js` | Auth plugin v1.0.0: API key validation |
| `plugin_auth_v2/index.js` | Auth plugin v2.0.0: API key + Bearer token |
| `test.sh` | Automated test: load v1 → verify → upgrade to v2 → verify |

## How to run

```bash
cd C3.4_Plugin_version_pinning
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# Check plugin registry
curl http://localhost:8171/plugins/

# Test v1 auth (API key)
curl -H "X-Api-Key: demo-key-1" http://localhost:8171/api/

# Upgrade to v2 (also accepts Bearer tokens)
curl "http://localhost:8171/admin/load-plugin/?name=acmecorp%2Fauth&version=2.0.0"

# Test v2 bearer token
curl -H "Authorization: Bearer bearer-token-xyz" http://localhost:8171/api/

# Registry shows v2
curl http://localhost:8171/plugins/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. Start nginx — registry shows `acmecorp/auth@1.0.0`.
2. `X-Api-Key: demo-key-1` works; no `Authorization` header.
3. Upgrade to v2 via `/admin/load-plugin/` — no reload.
4. Both API keys and Bearer tokens work with v2.
5. `/plugins/` confirms the registry updated to v2 with the new `loadedAt`
   timestamp.
6. In production: replace the hard-coded `fileMap` with a fetch from a
   plugin registry service, enabling full GitOps-style plugin management.
