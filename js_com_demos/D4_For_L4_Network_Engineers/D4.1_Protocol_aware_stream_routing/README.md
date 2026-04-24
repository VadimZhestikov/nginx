# D4.1 — Protocol-Aware Stream Routing

## What this demo shows

Routes incoming connections to different backends based on the application
protocol detected in the first bytes of the TCP stream. The routing logic is
demonstrated using an HTTP simulation (X-Protocol header), with the README
explaining the production stream{} implementation.

Routing table (simulated via X-Protocol header):
- `ssh` → SSH backend (OpenSSH compatible)
- `https` → HTTPS/TLS backend
- `http` → Plaintext HTTP backend
- anything else → reject with 400

Per-protocol counters in a SharedArrayBuffer provide live routing statistics
at `/status/` without any external metrics store.

## Production implementation (stream{} module)

In a production L4 deployment this uses the nginx `stream{}` block with the
JS stream module:

```nginx
stream {
    js_source stream_router.js;

    server {
        listen 443;
        js_preread detect_protocol;
        proxy_pass $upstream;
    }
}
```

```javascript
// stream_router.js
function detect_protocol(s) {
    s.on('upload', function(data, flags) {
        // SSH: first bytes are "SSH-2.0"
        if (data.startsWith('SSH-2.0')) {
            s.variables.upstream = 'ssh_backend';
        }
        // TLS: ClientHello starts with 0x16 0x03
        else if (data.charCodeAt(0) === 0x16 && data.charCodeAt(1) === 0x03) {
            s.variables.upstream = 'tls_backend';
        }
        // HTTP: starts with GET/POST/HEAD
        else if (/^(GET|POST|HEAD|PUT|DELETE) /.test(data)) {
            s.variables.upstream = 'http_backend';
        }
        s.done();
    });
}
```

## Why it is powerful

Classic nginx stream routing can only select backends based on IP address,
port, or SSL SNI. Protocol-level routing (a single port that handles SSH,
TLS, and plaintext HTTP) requires deep packet inspection — traditionally done
by specialized hardware or complex iptables/nftables rules.

With the JS stream module, protocol detection is a simple string comparison on
the first few bytes — pure JavaScript, no kernel modules, no separate
DPI appliance.

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Route to SSH backend
curl -H "X-Protocol: ssh" http://127.0.0.1:8190/connect/
# → SSH backend — connection accepted (OpenSSH 8.9 compatible)

# Route to HTTPS backend
curl -H "X-Protocol: https" http://127.0.0.1:8190/connect/
# → HTTPS backend — TLS handshake complete, serving secure content

# Route to HTTP backend
curl -H "X-Protocol: http" http://127.0.0.1:8190/connect/
# → HTTP backend — plaintext request served

# Unknown protocol
curl -H "X-Protocol: smtp" http://127.0.0.1:8190/connect/
# → HTTP 400 — Unknown protocol — connection rejected

# Routing statistics
curl http://127.0.0.1:8190/status/
# → {"routing_table":[...],"stats":{"ssh":1,"https":1,"http":1,"unknown":1}}

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```

## Notes

This demo uses HTTP-level simulation because the nginx JS stream module APIs
(`js_preread`, `s.on('upload', ...)`) differ from the HTTP module APIs
(`js_source`, `r.subrequest()`, `loc.addHook()`). The routing logic — reading
the first bytes and selecting a backend — is identical in both cases.

For the actual `stream{}` JS module API, see the nginx-njs documentation and
the test files in `t/js_stream_*.t`.
