# D4.3 — TLS Fingerprinting

## What this demo shows

Client classification and backend routing based on TLS/client fingerprints.
Browsers, bots, and API clients are identified and served different content —
without any application-layer changes. The demo simulates the fingerprint via
HTTP headers; the README explains the production `stream{}` implementation.

Classification hierarchy:
1. `X-TLS-Fingerprint` header matches known bot JA3 hash → bot backend
2. User-Agent contains bot/crawler/spider pattern → bot backend
3. User-Agent matches API client pattern (okhttp, axios, go-http) → API backend
4. Default → browser backend

Routing statistics are tracked in a SharedArrayBuffer and exposed at
`/status/`.

## Production implementation (stream{} module)

In production this runs in the `stream{}` block, inspecting the TLS
ClientHello BEFORE the handshake completes:

```nginx
stream {
    js_source fingerprint.js;

    server {
        listen 443;
        js_preread compute_ja3;
        proxy_pass $upstream;
    }
}
```

```javascript
// fingerprint.js
function compute_ja3(s) {
    s.on('upload', function(data, flags) {
        // Parse TLS ClientHello (type=0x16, version=0x0303)
        if (data.charCodeAt(0) !== 0x16) { s.done(); return; }

        var ja3 = extractJA3(data);  // parse cipher suites, extensions, curves
        var hash = md5(ja3);          // JA3 fingerprint

        if (BOT_FINGERPRINTS[hash]) {
            s.variables.upstream = 'bot_backend';
        } else {
            s.variables.upstream = 'browser_backend';
        }
        s.done();
    });
}
```

JA3 is computed from:
- TLS version
- Cipher suites (sorted)
- Extensions list
- Elliptic curves
- Elliptic curve point formats

Each client (curl, Chrome, Firefox, Python-requests, Java HttpClient) produces
a unique JA3 string → distinct MD5 hash.

## Why it is powerful

Traditional nginx can only route based on SNI (server name) from the
ClientHello — a single string. JA3 fingerprinting extracts 5 fields from the
ClientHello and produces a hash that identifies the TLS library/version of the
client, enabling:

- Blocking known bad actors by fingerprint (cannot be spoofed by changing IP)
- Serving bots a lightweight version (no JS, cached) → reduced compute cost
- API clients get JSON directly without rendering HTML
- Human browsers get the full interactive experience

All decisions happen at the TLS handshake phase, before any HTTP request is
parsed.

## Classic nginx approach

```nginx
# Can only inspect SNI — no cipher suite / extension fingerprinting
map $ssl_server_name $backend {
    ~*bot.example.com   bot_upstream;
    default             browser_upstream;
}
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Browser (default UA) → browser backend
curl -H "User-Agent: Mozilla/5.0 (X11; Linux x86_64)" \
     http://127.0.0.1:8192/detect/
# → <html><body><h1>Welcome, human!</h1>...

# Googlebot → bot backend
curl -H "User-Agent: Googlebot/2.1" http://127.0.0.1:8192/detect/
# → bot detected — serving limited content

# Known bot fingerprint (human-looking UA, but busted by JA3)
curl -H "User-Agent: Mozilla/5.0 (appears human)" \
     -H "X-TLS-Fingerprint: a0e9f5d64349fb13191bc781f81f42e1" \
     http://127.0.0.1:8192/detect/
# → bot detected — serving limited content

# API client → API backend
curl -H "User-Agent: okhttp/4.10.0" http://127.0.0.1:8192/detect/
# → {"status":"ok","client":"api","version":"1.0"}

# Routing statistics
curl http://127.0.0.1:8192/status/
# → {"routing_stats":{"browser":1,"bot":2,"api":1,"unknown":0},...}

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

The `X-TLS-Fingerprint` header in this demo simulates what the stream{} module
would compute from the raw ClientHello bytes. In production deployments the
JA3 hash is computed by a C helper function (for performance) and passed to
the JavaScript routing logic via a variable — same routing code, different
input source.

For MD5 computation in the stream module, use the nginx `md5` built-in or a
native C function registered via the JS FFI.
