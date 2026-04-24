# B1.2 — Route on Request Body (GraphQL-style)

## What it shows

Reads the POST body as JSON, inspects the `operation` field, and routes the
request to a different internal location — all inside a single nginx worker, no
external process involved.

| `operation` value | Internal backend |
|---|---|
| `GetUser`, `ListUsers` | `/internal/users/` |
| `CreateOrder`, `GetOrder` | `/internal/orders/` |
| anything else | 400 Bad Request |

## Classic nginx comparison

Classic nginx cannot read the request body to make routing decisions.  The only
way to achieve body-based routing with stock nginx is to proxy everything to an
application server that re-dispatches internally — adding a full round-trip and
an extra process.  With JavaScript, `await r.readBody()` is a single await and
`JSON.parse()` is a one-liner.

## How to run

```bash
cd B1.2_Route_on_request_body
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Route to users handler
curl -X POST -H "Content-Type: application/json" \
    -d '{"operation":"GetUser"}' \
    http://localhost:8133/graphql/

# Route to orders handler
curl -X POST -H "Content-Type: application/json" \
    -d '{"operation":"CreateOrder"}' \
    http://localhost:8133/graphql/

# Unknown operation → 400
curl -X POST -H "Content-Type: application/json" \
    -d '{"operation":"Hack"}' \
    http://localhost:8133/graphql/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
