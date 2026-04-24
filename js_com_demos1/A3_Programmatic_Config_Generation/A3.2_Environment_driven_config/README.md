# A3.2 — Environment-Driven Config Generation

## What this demo shows

`js_preprocess gen.js` reads `APP_ENV` at **config parse time** using
`std.getenv('APP_ENV')` and generates a different `http {}` block based on the
environment:

| `APP_ENV` | Port | Server characteristics |
|-----------|------|----------------------|
| `dev`     | 8118 | Debug info in responses, relaxed settings |
| `prod`    | 8119 | Minimal responses, production tuning |

The **same `nginx.conf`** works in all environments. The environment variable
is the only differentiator — no per-environment config files to maintain.

## Classic nginx approach

Classic nginx requires separate config files per environment (or complex
`include` + `map` gymnastics), external template rendering via CI/CD, or
environment-specific Docker images with baked-in configs.

## Key API

```javascript
// gen.js (js_preprocess script)
import * as std from 'std';

var env = std.getenv('APP_ENV') || 'dev';

if (env === 'prod') {
    config.write(prodConfig);
} else {
    config.write(devConfig);
}
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start in dev mode
APP_ENV=dev ../../../objs/nginx -p . -c nginx.conf
curl http://127.0.0.1:8118/
# → env=dev port=8118 [debug mode]
../../../objs/nginx -p . -c nginx.conf -s stop

# Start in prod mode — same nginx.conf, different environment
APP_ENV=prod ../../../objs/nginx -p . -c nginx.conf
curl http://127.0.0.1:8119/
# → env=prod port=8119
../../../objs/nginx -p . -c nginx.conf -s stop
```
