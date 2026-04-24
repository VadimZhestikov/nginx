# A1.5 — Feature Flag a Location

## What this demo shows

A boolean flag in JS state gates access to an endpoint at runtime. Toggling
the flag via an admin endpoint instantly enables or disables the feature for
all subsequent requests — no config file change, no reload.

Multiple independent flags can coexist in a single flags registry. The pattern
scales naturally: flags can be stored in `nginx.shared` for cross-worker
visibility (see A2.2), or synced from an external config service.

## Classic nginx approach

Enabling a location conditionally in classic nginx requires either:
- Editing `nginx.conf` and reloading, or
- Using `geo` / `map` variables combined with `return 404`, which are static

There is no way to flip a feature flag at runtime without a reload.

## Key pattern

```javascript
var flags = { beta: false, darkMode: false };

loc.handler = function(r) {
    if (!flags.beta) {
        r.respond(404, {}, 'Feature disabled\n');
        return;
    }
    r.respond(200, {}, 'Beta feature content\n');
};

// Admin toggle (called from another handler):
flags.beta = !flags.beta;
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Beta is off by default
curl http://127.0.0.1:8107/beta/
# → 404: beta feature is disabled

# Enable it at runtime — no reload
curl -X POST "http://127.0.0.1:8107/admin/toggle/?beta"
# → beta=true

# Beta is now live
curl http://127.0.0.1:8107/beta/
# → beta feature is enabled

# List all flags
curl http://127.0.0.1:8107/admin/flags/
# → beta=true
# → darkMode=false
# → newCheckout=false

# Disable again
curl -X POST "http://127.0.0.1:8107/admin/toggle/?beta"
curl http://127.0.0.1:8107/beta/
# → 404

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
