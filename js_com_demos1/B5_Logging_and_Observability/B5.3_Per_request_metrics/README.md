# B5.3 — Per-Request Latency Histogram + Prometheus /metrics

## What it shows

Records per-request latency in a **4-bucket histogram** stored in a
`SharedArrayBuffer`, then exposes it at `/metrics/` in Prometheus text format
(exposition format 0.0.4).

### Histogram buckets

| Bucket | Latency range |
|---|---|
| `le="10"` | < 10 ms |
| `le="50"` | 10–50 ms |
| `le="200"` | 50–200 ms |
| `le="+Inf"` | > 200 ms |

Because the SAB is allocated **before `fork()`**, all nginx workers share the
same physical memory pages.  `Atomics.add` provides lock-free atomic updates —
no mutex required.

### Example `/metrics` output

```
# HELP http_request_duration_bucket Latency histogram (ms)
# TYPE http_request_duration_bucket counter
http_request_duration_bucket{le="10"} 12
http_request_duration_bucket{le="50"} 12
http_request_duration_bucket{le="200"} 12
http_request_duration_bucket{le="+Inf"} 12
http_requests_total 12
http_request_duration_sum 47
```

## Classic nginx comparison

`stub_status` exposes four integers (active connections, accepts, handled,
requests).  `nginx-module-vts` adds per-virtualhost traffic statistics.
Neither tracks latency.  `nginx-prometheus-exporter` is a sidecar binary that
reads `stub_status` externally — no latency either.

Implementing a latency histogram with stock nginx requires either Lua +
`lua_shared_dict` (OpenResty) or an external exporter with custom
instrumentation.  The JS SAB + Atomics solution needs no extra process, no
extra module, and no shared memory zone declaration in nginx.conf.

## How to run

```bash
cd B5.3_Per_request_metrics
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Generate some traffic
for i in $(seq 1 10); do curl -s http://localhost:8150/api/ > /dev/null; done

# Scrape metrics (pipe to jq for pretty view if unavailable, use grep)
curl http://localhost:8150/metrics/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
