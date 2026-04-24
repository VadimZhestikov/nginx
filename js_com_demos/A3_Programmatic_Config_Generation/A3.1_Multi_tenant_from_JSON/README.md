# A3.1 — Multi-Tenant Server Generation from JSON

## What this demo shows

`js_preprocess gen.js` runs **during nginx config parsing** — before any
`http {}` block is evaluated. The script reads `tenants.json`, parses it, and
calls `config.write()` to inject fully-formed `server {}` blocks into the
config stream. The injected blocks receive the full nginx merge/init_locations
treatment as if they had been written by hand.

Result: three tenant servers (ports 8115, 8116, 8117) generated from data,
with zero hand-written `server {}` blocks in `nginx.conf`.

## Classic nginx approach

Classic nginx requires a template engine (e.g. `envsubst`, `gomplate`, ERB) as
an external pre-processing step, producing a static `nginx.conf` file. Adding a
tenant means regenerating the file and reloading nginx. With `js_preprocess` the
source of truth is the JSON file; nginx itself does the generation at parse time.

## Key API

```javascript
// gen.js (js_preprocess script)
import * as std from 'std';

var tenants = JSON.parse(std.loadFile('./tenants.json'));

tenants.forEach(function(t) {
    config.write(`
        server {
            listen ${t.port};
            server_name ${t.name}.local;
            location / { return 200 "${t.greeting}\\n"; }
        }
    `);
});
```

## Files

| File | Purpose |
|------|---------|
| `nginx.conf` | Boilerplate only — no server blocks |
| `gen.js` | `js_preprocess` script that generates server blocks |
| `tenants.json` | Data source: name, port, greeting per tenant |

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx — config is generated from tenants.json at parse time
../../../objs/nginx -p . -c nginx.conf

# Each tenant has its own port and customised greeting
curl http://127.0.0.1:8115/
# → Welcome to ACME Corp

curl http://127.0.0.1:8116/
# → Welcome to Beta Inc

curl http://127.0.0.1:8117/
# → Welcome to Gamma Labs

# Add a new tenant: edit tenants.json, reload nginx
# No template engine, no shell script — just JSON + reload

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
