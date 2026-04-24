# D2.3 — WAF Lite: Request Body Scanner

## What this demo shows

A lightweight Web Application Firewall (WAF) implemented as a location hook
that reads the request body with `await r.readBody()` and scans it against
SQL injection and XSS pattern lists. Malicious requests receive `400 Bad
Request` before the content handler ever runs — no external WAF appliance, no
reverse proxy chain, no Lua scripts.

Key mechanics:
- `loc.addHook(async fn)` — async hooks can `await r.readBody()` before the
  content handler runs
- Pattern matching is case-insensitive (body lowercased for comparison)
- Clean requests fall through to the content handler normally
- Rule set is a plain JS array — patterns can be added or removed at runtime
  without reloading nginx

## Why it is powerful

Traditional nginx WAF integration requires:
1. ModSecurity or NAXSI compiled as a module (complex build)
2. Lua + lua-resty-waf (OpenResty only)
3. An external reverse-proxy WAF (Cloudflare, AWS WAF) adding latency

With JS hooks the scan runs in the nginx worker process with no network
round-trip. The pattern list is a plain JavaScript array that can be updated
via an API call at runtime.

## Classic nginx approach

```nginx
# Requires ModSecurity or Lua (OpenResty)
location /api/submit/ {
    # ModSecurity engine — complex C module
    modsecurity on;
    modsecurity_rules_file /etc/nginx/modsec/main.conf;
    proxy_pass http://backend;
}
```

## Key API

```javascript
// Async hook reads body before content handler
loc.addHook(async function(r) {
    var body = await r.readBody();
    if (containsInjection(body)) {
        r.respond(400, {'X-WAF-Blocked': 'sqli'}, 'Blocked by WAF\n');
        return;  // content handler skipped
    }
    // fall through — content handler runs
});
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Inspect current WAF rules
curl http://127.0.0.1:8182/waf/rules/
# → {"total_rules":21,"by_type":{"sqli":13,"xss":9},...}

# Clean submission — passes through
curl -X POST -H "Content-Type: application/json" \
     -d '{"name":"Alice","email":"alice@example.com"}' \
     http://127.0.0.1:8182/api/submit/
# → {"status":"ok","message":"Submission accepted"}

# SQL injection — blocked immediately
curl -i -X POST -d "' OR 1=1 --" http://127.0.0.1:8182/api/submit/
# → HTTP/1.1 400 Bad Request
# → Blocked by WAF: sqli pattern detected

# XSS — blocked
curl -i -X POST -d '<script>alert(document.cookie)</script>' \
     http://127.0.0.1:8182/api/submit/
# → HTTP/1.1 400 Bad Request
# → Blocked by WAF: xss pattern detected

# DROP TABLE — blocked
curl -i -X POST -d "'; DROP TABLE users; --" http://127.0.0.1:8182/api/submit/
# → HTTP/1.1 400 Bad Request

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

`r.readBody()` buffers the complete request body in the worker process before
the hook returns. For payloads larger than a few megabytes, use streaming body
filters (`loc.addBodyFilter('streaming', asyncGeneratorFn)`) to scan
chunk-by-chunk without buffering the entire body in memory.

The pattern list in this demo is intentionally short for readability. A
production deployment would include the full OWASP Core Rule Set patterns
(loaded from a JSON file at startup) and optionally a native regex engine
called via the JS FFI for performance-critical rules.
