# B5.2 — Structured JSON Access Log

## What it shows

A request hook (`addHook`) fires before every handler and emits a single-line
JSON log entry to `logs/error.log` via `nginx.log`.  Each entry includes:

| Field | Value |
|---|---|
| `ts` | ISO-8601 timestamp |
| `method` | HTTP verb |
| `uri` | request URI |
| `ua` | User-Agent (truncated to 80 chars) |
| `ref` | Referer header |
| `wid` | worker index (`nginx.workerIdx`) |

Example output:
```
2026-04-23T12:34:56.789Z [info] access {"ts":"2026-04-23T12:34:56.789Z","method":"GET","uri":"/api/","ua":"curl/8.0","ref":"","wid":0}
```

## Classic nginx comparison

`log_format` supports custom formats, but building valid JSON is fragile: any
field that contains a double-quote, backslash, or control character will silently
corrupt the output.  The usual workaround is to escape each field individually
with `$variable` — error-prone and hard to maintain.  `JSON.stringify` in
JavaScript handles all escaping automatically and the log entry is defined as a
plain object literal, making it easy to add or remove fields.

## How to run

```bash
cd B5.2_Structured_JSON_log
bash test.sh
```

After the test, `logs/error.log` contains JSON-formatted entries that can be
piped to `jq` or any structured log aggregator.

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Make a request
curl -A "MyApp/2.0" http://localhost:8149/api/

# Inspect the log
cat logs/error.log | grep '"method"'

# Pretty-print with jq (if available)
cat logs/error.log | grep '"method"' | sed 's/.*access //' | jq .

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
