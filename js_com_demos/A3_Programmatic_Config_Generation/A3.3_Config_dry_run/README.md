# A3.3 — Config Dry Run (Location Match Simulator)

## What this demo shows

`nginx.http.match(uri [, serverName])` simulates nginx's complete location-
matching algorithm — exact matches, preferential prefixes (`^~`), regex, and
plain prefix fallback — and returns the location object that would handle a
given URI, **without processing an actual request**.

Use cases:

- **Config validation**: verify routing rules behave as expected before
  deploying a config change
- **Debugging**: answer "which location handles `/api/v2/users`?" at a glance
- **CI testing**: unit test routing logic with `curl /match/?<uri>`
- **Admin UI**: render a routing table showing all locations and their patterns

## Classic nginx approach

There is no built-in way to introspect nginx's routing decisions. Developers
must send real requests and infer from responses, or use `--test` mode which
only validates syntax. Routing bugs are often discovered in production.

## Key API

```javascript
// Simulate which location handles /api/v2/users on server app.local
var loc = nginx.http.match('/api/v2/users', 'app.local');
console.log(loc ? loc.pattern : 'no match');
// → "/api/v2/"

// Omit server name to use the first (default) server
var loc2 = nginx.http.match('/static/img/logo.png');
console.log(loc2.pattern);
// → "^~ /static/"
```

## How to run

```bash
bash test.sh
```

## Manual demo steps (for a presenter)

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Simulate routing without sending traffic to the real handler
curl "http://127.0.0.1:8120/match/?/health"
# → {"uri":"/health","matched":"= /health","type":"exact"}

curl "http://127.0.0.1:8120/match/?/api/v1/users"
# → {"uri":"/api/v1/users","matched":"/api/v1/","type":"prefix"}

curl "http://127.0.0.1:8120/match/?/static/css/app.css"
# → {"uri":"/static/css/app.css","matched":"^~ /static/","type":"prefix"}

curl "http://127.0.0.1:8120/match/?/unknown"
# → {"uri":"/unknown","matched":"/","type":"prefix"}

# Real requests still work normally
curl http://127.0.0.1:8120/api/v2/orders
# → API v2

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
