# B3.2 — Conditional HTML Injection

## What it shows

A body filter inspects the **request cookie** and, when a `session` cookie is
present, splices a `<script>` tag into the HTML response immediately before
`</head>`.  Anonymous visitors receive the page unmodified.

This pattern is used to:
- Inject per-user analytics or A/B test scripts
- Enable debug toolbars for authenticated staff
- Add feature-flag loaders for opted-in users

…without changing the upstream application at all.

## Classic nginx comparison

`ngx_http_addition_module` appends/prepends static content regardless of the
request.  `ngx_http_sub_module` replaces a fixed literal, but cannot branch on
a request header or cookie.  Conditional injection requires OpenResty (Lua) or
a middleware layer.  The JS body filter receives both the response body and the
original request object, so the cookie check and string splice are two lines of
plain JavaScript.

## How to run

```bash
cd B3.2_Conditional_HTML_injection
bash test.sh
```

## Manual demo steps

```bash
# Start nginx
../../../objs/nginx -p . -c nginx.conf

# Anonymous visitor — no script tag
curl http://localhost:8139/page/

# Authenticated visitor — script is injected before </head>
curl -b "session=abc123" http://localhost:8139/page/

# Stop nginx
../../../objs/nginx -p . -c nginx.conf -s stop
```
