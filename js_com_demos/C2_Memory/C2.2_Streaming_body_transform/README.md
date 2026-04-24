# C2.2 — Streaming Body Transform (Generator Filter)

## What it shows

An `async function*` generator body filter processes the response body as a
stream of chunks.  This demo strips all comment lines (starting with `#`)
from a configuration-style text response before delivering it to the client.

Compare `/passthrough/` (raw body with comments) to `/data/` (filtered body
without comments) to see the difference.

## Why classic NGINX cannot do this

Classic NGINX can modify responses with `sub_filter` (exact string replacement)
or SSI.  Neither supports line-level filtering logic.  The `ngx_http_sub_module`
replaces fixed patterns; there is no way to express "remove all lines matching
a condition" without an external service.  The JS generator filter runs entirely
in-process with full programmatic control over every byte.

## Files

| File | Purpose |
|---|---|
| `nginx.conf` | Server on port 8164 |
| `handler.js` | Handler + streaming generator body filter |
| `test.sh` | Automated test: comments absent, data lines present |

## How to run

```bash
cd C2.2_Streaming_body_transform
bash test.sh
```

Or manually:

```bash
../../../objs/nginx -p . -c nginx.conf

# Raw body (comments included)
curl http://localhost:8164/passthrough/

# Filtered body (comments stripped)
curl http://localhost:8164/data/

../../../objs/nginx -p . -c nginx.conf -s stop
```

## Demo steps

1. `GET /passthrough/` shows the raw text including `# comment` lines.
2. `GET /data/` shows the same text with all `#` lines removed and
   consecutive blank lines collapsed.
3. The filter is attached with `addBodyFilter('generator', async function* ...)`.
   The generator accumulates all chunks, applies the line filter, and `yield`s
   the cleaned output as a single chunk.
4. Extend this pattern to: redact secrets, inject headers into YAML/TOML
   responses, compress CSV columns, or stream-parse JSON arrays.
