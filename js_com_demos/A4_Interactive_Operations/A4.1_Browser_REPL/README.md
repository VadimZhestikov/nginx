# A4.1 — Browser REPL

## What this demo shows

A browser-based Read-Eval-Print Loop that connects to a running nginx instance
and evaluates JavaScript expressions against the live COM object tree. This
provides an interactive exploration environment for:

- Querying live server state (`nginx.http.servers`, `nginx.http.upstreams`)
- Executing mutations (`server.addLocation`, `loc.handler = ...`)
- Testing JS snippets against real nginx configuration
- Demonstrating the COM API interactively to an audience

## Classic nginx approach

There is no interactive console for nginx. Configuration is static text;
introspection requires reading files or using external tools like `nginx -T`.
Any change requires editing config files and reloading.

## How it works

The browser REPL is provided by the **admin UI application** (`admin_UI_demo_v2`
or `admin_ui_demo`) in the `js_com_apps/` directory. Start the admin UI and
open it in a browser to access the REPL tab.

## How to run

```bash
# See the admin UI application in js_com_apps/
ls ../../js_com_apps/

# Follow the README in the relevant app directory
```

## Key concept

```javascript
// Expressions typed in the browser REPL execute in the nginx worker context:
nginx.http.servers.map(s => s.name)
// → ["app.local", "api.local", "static.local"]

nginx.http.servers[0].locations.map(l => l.path)
// → ["/", "/api/", "/static/"]

// Mutations take effect immediately:
nginx.http.servers[0].locations.find(l => l.path === '/').handler = r => {
    r.respond(200, {}, 'REPL handler active!\n');
};
```
