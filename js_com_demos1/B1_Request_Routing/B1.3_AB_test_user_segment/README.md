# B1.3 — A/B Test User Segment Routing

## What it shows

Routes visitors to landing page variants based on a **cookie value** and the
**current hour of day** — two independent conditions combined in plain
JavaScript.

| `segment` cookie | Hour | Variant served |
|---|---|---|
| `beta` | any | `landing B` |
| (absent) | >= 18 | `landing A evening` |
| (absent) | < 18 | `landing A` |

The response also carries an `X-Variant` header so downstream caches and
analytics can record which variant was shown.

## Classic nginx comparison

Classic nginx can read a cookie via `$cookie_segment` and compare it in an
`if` block.  However, extracting the current hour requires the `$time_local`
variable and a `map` that parses a locale-formatted date string — fragile and
non-obvious.  Combining cookie + time in a single routing decision is not
straightforward.  In JavaScript, `new Date().getHours()` is a one-liner and the
logic reads naturally.

## How to run

```bash
cd B1.3_AB_test_user_segment
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Beta user always gets landing B
curl -b "segment=beta" http://localhost:8134/landing/

# Default user — response depends on current server hour
curl http://localhost:8134/landing/

# Inspect X-Variant header
curl -I -b "segment=beta" http://localhost:8134/landing/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
