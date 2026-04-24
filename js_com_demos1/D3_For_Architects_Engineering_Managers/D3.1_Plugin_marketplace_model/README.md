# D3.1 — Plugin Marketplace Model

## What this demo shows

`nginx.install(plugin, config)` composes independent behaviour modules onto
existing locations without modifying the base configuration. Three plugins are
installed in sequence:

- `ratelimit@1.0` — token-bucket rate limiter (SharedArrayBuffer, lock-free)
- `cors@2.1` — CORS header injection via a location hook
- `telemetry@1.0` — request counter exposed at `/metrics/`

Each plugin is a plain JS object with an `.install(config)` method. The
plugin installs its own hooks and handlers onto existing locations, then
returns. The caller never needs to know the plugin's internals.

## Why it is powerful

In classic nginx, composing multiple cross-cutting concerns (rate limiting,
CORS, metrics) requires:
- Multiple `limit_req_zone` / `add_header` / `log_format` directives
- Third-party modules compiled into the binary
- Careful ordering of directives across `nginx.conf`, `conf.d/`, and
  `snippets/`

With `nginx.install()` each concern is a self-contained JavaScript module.
Teams can publish plugins to a registry, version them, configure them, and
compose them in any order — all in JavaScript, all testable in isolation.

## Classic nginx approach

```nginx
# Each concern requires its own directive/module
limit_req_zone $binary_remote_addr zone=api:10m rate=10r/s;
add_header Access-Control-Allow-Origin * always;
# Metrics require stub_status or an external exporter

location /api/ {
    limit_req zone=api burst=5;
    proxy_pass http://backend;
}
```

## Key API

```javascript
// Inline plugin — object with .install method
var corsPlugin = {
    install: function(cfg) {
        loc.addHook(function(r) {
            r.setHeader('Access-Control-Allow-Origin', cfg.origin || '*');
        });
    }
};

nginx.install(corsPlugin, { origin: 'https://app.example.com' });

// Function-style plugin
nginx.install(function(cfg) {
    loc.addHook(function(r) { /* ... */ });
}, { config: 'value' });

// Filesystem plugin (tracked in nginx.plugins array)
nginx.use('/path/to/plugin-directory', { config: 'value' });
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# List installed plugins
curl http://127.0.0.1:8183/plugins/
# → {"installed":[{"name":"ratelimit","version":"1.0",...},{"name":"cors",...},...]

# CORS headers are present on every /api/ response
curl -i http://127.0.0.1:8183/api/
# → Access-Control-Allow-Origin: *
# → X-RateLimit-Remaining: 9

# OPTIONS preflight (cors plugin)
curl -i -X OPTIONS http://127.0.0.1:8183/api/
# → HTTP/1.1 204 No Content

# Metrics from telemetry plugin
curl http://127.0.0.1:8183/metrics/
# → {"api_requests_total":3,"ratelimit_tokens_remaining":7,"ratelimit_max":10}

# Rate limit in action
for i in $(seq 1 20); do curl -s http://127.0.0.1:8183/api/; done
# → last few return: Too Many Requests

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
