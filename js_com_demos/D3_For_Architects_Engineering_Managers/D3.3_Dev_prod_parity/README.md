# D3.3 — Dev/Prod Parity

## What this demo shows

The same `nginx.conf` and `gen.js` generate completely different server
configurations depending on the `APP_ENV` environment variable, using
`js_preprocess`. This guarantees dev/prod parity through a single shared
code path — the only external input is the environment variable.

| Environment | Port | /api/ | /debug/ | /error/ |
|-------------|------|-------|---------|---------|
| `dev`       | 8184 | Verbose JSON with debug info | Present | Detailed error |
| `prod`      | 8185 | Minimal `{"message":"OK"}` | Absent (404) | Generic message |

Key mechanics:
- `js_preprocess gen.js` runs at nginx parse time — before `init_conf`
- `gen.js` reads `APP_ENV` via `std.getenv('APP_ENV')`
- `config.write(text)` emits the complete `http{}` block with the appropriate
  port and location set
- `handler.js` reads `APP_ENV` again at runtime to adjust response verbosity
- The `/debug/` location is only emitted in `dev` — it doesn't exist at all
  in prod (not just 403 — completely absent from the config)

## Why it is powerful

Traditional nginx has no mechanism to conditionally generate config based on
environment variables. Teams work around this with:
- Multiple `nginx.conf` files (`nginx.dev.conf`, `nginx.prod.conf`) that
  diverge over time
- ERB/Jinja templates processed by Ansible/Puppet before deployment
- Environment-specific Docker images with baked-in configs

With `js_preprocess` the environment differentiation is a `3-line if/else`
in the same file that serves all environments. There is no divergence, no
template engine dependency, no separate build step.

## Classic nginx approach

```bash
# Must maintain separate files or use a template engine
envsubst < nginx.conf.template > /etc/nginx/nginx.conf
nginx -s reload
```

```nginx
# nginx.dev.conf — maintained separately, diverges from prod over time
server { listen 8184; location /debug/ { ... } }

# nginx.prod.conf — different file, same risk of divergence
server { listen 8185; }
```

## Key API

```javascript
// gen.js (js_preprocess) — runs at parse time
import * as std from 'std';
var env  = std.getenv('APP_ENV') || 'dev';
var port = env === 'prod' ? 8185 : 8184;

config.write('http { server { listen ' + port + '; ... } }');

// handler.js (js_source) — runs at init_conf time
import * as std from 'std';
var isDev = (std.getenv('APP_ENV') || 'dev') === 'dev';
loc.handler = function(r) {
    r.respond(200, {}, isDev ? verboseResponse : minimalResponse);
};
```

## How to run

```bash
bash test.sh
```

The test script starts nginx twice — once as `dev` and once as `prod` — and
verifies the different behaviour on the respective ports.

## Manual demo steps (for a presenter)

```bash
# Dev environment — port 8184, verbose responses
APP_ENV=dev ../../../objs/nginx -p . -c nginx.conf
curl http://127.0.0.1:8184/env/
# → dev
curl http://127.0.0.1:8184/api/
# → {"message":"Hello from dev","environment":"dev","debug_info":{...}}
curl http://127.0.0.1:8184/debug/
# → {"environment":"dev","nginx_version":"...",...}
../../../objs/nginx -p . -c nginx.conf -s stop

# Prod environment — port 8185, minimal responses
APP_ENV=prod ../../../objs/nginx -p . -c nginx.conf
curl http://127.0.0.1:8185/env/
# → prod
curl http://127.0.0.1:8185/api/
# → {"message":"OK"}
curl http://127.0.0.1:8185/debug/
# → 404 Not Found  (location not emitted in prod)
../../../objs/nginx -p . -c nginx.conf -s stop
```
