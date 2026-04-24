# C4.3 — Config Linting Without NGINX

## What it shows

A schema validator (`validate-config.js`) checks plugin configuration JSON
against rules for required fields, type constraints, numeric ranges, and
enumerated values — entirely with `qjs`, no NGINX needed.

The same validation logic is also exposed as `POST /validate/` inside NGINX
(`handler.js`), so deployment tooling can call the same schema at runtime.
One source of truth, two execution contexts.

Checks performed:

| Rule | Example |
|---|---|
| Required fields | `port`, `maxConnections` must be present |
| Type check | `debug` must be `boolean`, not `"yes"` |
| Range check | `port` in [1, 65535]; `maxConnections` in [1, 10000] |
| Enum check | `logLevel` must be one of `debug/info/warn/error` |

## Why classic NGINX cannot do this

Classic NGINX has no concept of plugin configuration validation.  Typos in
upstream config, wrong types, or out-of-range values cause runtime failures
or silently wrong behaviour.  The JS validator catches these mistakes before
deployment, in CI, with a simple `qjs validate-config.js config.json` step.

## Files

| File | Purpose |
|---|---|
| `validate-config.js` | CLI validator: `qjs validate-config.js <file>` |
| `good-config.json` | Valid config (all rules satisfied) |
| `bad-config.json` | Invalid config (5 distinct errors) |
| `handler.js` | Same schema as `POST /validate/` nginx endpoint |
| `nginx.conf` | Server on port 8174 |
| `test.sh` | Runs qjs checks + nginx HTTP endpoint checks |

## How to run

```bash
cd C4.3_Config_linting_without_nginx

# Validate good config — should print "OK: config is valid"
qjs --std validate-config.js good-config.json

# Validate bad config — should list 5 errors and exit 1
qjs --std validate-config.js bad-config.json

# Full test (qjs + nginx)
bash test.sh
```

Or via nginx HTTP endpoint:

```bash
../../../objs/nginx -p . -c nginx.conf

curl -X POST -H "Content-Type: application/json" \
     -d @good-config.json http://localhost:8174/validate/

curl -X POST -H "Content-Type: application/json" \
     -d @bad-config.json http://localhost:8174/validate/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## bad-config.json errors

1. `port: 99999` — out of range [1, 65535]
2. `debug: "yes"` — expected boolean, got string
3. `maxConnections` missing — required field
4. `logLevel: "verbose"` — not in [debug, info, warn, error]
5. `timeout: 50` — out of range [100, 300000]
