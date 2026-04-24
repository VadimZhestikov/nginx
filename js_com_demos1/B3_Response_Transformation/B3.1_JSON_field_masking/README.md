# B3.1 — JSON Field Masking

## What it shows

A **body filter** (`addBodyFilter`) intercepts the JSON response before it
leaves nginx and:

- Replaces the `ssn` field value with `"***"`
- Deletes the `internal` field entirely
- Passes all other fields through unchanged

The raw handler returns full PII-containing JSON; the filter sits in front and
sanitises it so that even if the upstream is misconfigured, sensitive data never
reaches the client.

## Classic nginx comparison

`ngx_http_sub_module` can replace fixed literal strings, but it has no JSON
awareness: it cannot target a specific field value while leaving the surrounding
structure intact.  Masking a value that changes per-response (or is embedded in
a larger object) requires a dedicated reverse proxy application.  The JS body
filter receives the full response body as a `string`, calls `JSON.parse`,
modifies the object, and returns `JSON.stringify` — plain JavaScript, no extra
process.

## How to run

```bash
cd B3.1_JSON_field_masking
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# The raw response (ssn and internal present) — body filter masks them before delivery
curl http://localhost:8138/data/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
