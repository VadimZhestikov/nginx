# 09 — Plugin System (P7 / P16 / P18)

## Purpose

Plugins let administrators load self-contained JS packages into a running nginx
without recompiling.  A plugin declares its configuration schema, registers
hooks and filters, and may expose a COM subtree under `nginx.plugins.*`.

Three steps build on each other:

| Step | Capability |
|---|---|
| P7  | `nginx.use(path, config)` during init_conf |
| P16 | `nginx.use(path, config)` callable post-fork (hot-reload) |
| P18 | `nginx.use('vendor/pkg@version', config)` — registry-resolved packages |

---

## P7: Basic Plugin Loading

### Package Layout
```
my-plugin/
  package.json    — {"name": "my-plugin", "version": "1.0.0", "main": "index.js"}
  index.js        — main entry point
  …
```

### Loading
```javascript
// In a js_source init script:
nginx.use('/opt/plugins/my-plugin', {
    logLevel: 'debug',
    upstream: 'backend'
});
```

`nginx.use(path, config)` in `ngx_js_com.c`:
1. Reads `package.json` → resolves the main file.
2. Calls `JS_Eval` on the main file with `path` as the module name.
3. Passes `config` as the second argument if the main export is a function:
   ```javascript
   // index.js
   module.exports = function(nginx, config) {
       var loc = nginx.http.servers[0].locations.find(...);
       loc.addHook(async function(req, next) { ... });
   };
   ```
4. Registers the plugin in the global plugin table:
   ```c
   typedef struct {
       ngx_str_t  path;
       JSValue    config;   /* JS object passed to use() */
       JSValue    instance; /* return value from the main function */
   } ngx_js_plugin_t;
   ```

The plugin table lives in `ngx_js_conf_t` and is accessible as
`nginx.plugins` (array of `{path, config}` objects).

---

## P16: Post-Fork Hot-Reload

P7 plugins are loaded during `init_conf` (before fork).  P16 extends the same
`nginx.use()` API to work from a request handler after fork:

```javascript
loc.handler = async function(req) {
    await nginx.use('/opt/plugins/monitoring-v2', {});
    req.respond(200, {}, 'reloaded');
};
```

Hot-reload is per-worker: only the worker that processes the request loads the
plugin.  To reload across all workers, use `nginx.broadcast()` with a reload
message + `nginx.on('message', ...)` handler that calls `nginx.use(...)`.

### Implementation
`nginx.use()` in hot-reload mode (`ngx_js_worker_t` exists, so we are post-fork):
1. Same eval path as P7.
2. The plugin's hooks/filters are added to the existing location config arrays.
3. No need to re-synthesise the hook chain for that location — `addHook` appends
   to `loc->hook_fns[]` dynamically; the chain is rebuilt on the next request.

---

## P18: Registry Resolution

P18 adds a local package registry so plugins can be referenced by name and version:

```javascript
nginx.use('acmecorp/auth-middleware@1.2.x', { tokenSecret: '...' });
```

### Registry Layout
```
/opt/nginx-plugins/
  registry.json   — {"acmecorp/auth-middleware": {"1.2.3": "./auth-1.2.3/"}}
  auth-1.2.3/
    package.json
    index.js
```

### Resolution Algorithm
1. Parse `vendor/name@semver` from the `use()` argument.
2. Read `registry.json` (cached after first load).
3. Find the best matching version using semver range matching.
4. Resolve to the local directory path.
5. Proceed as in P7 with the resolved path.

### No Network I/O at Runtime
The registry is always a local file-system lookup.  Package download is a
separate out-of-band operation (CI/CD, package manager).  This avoids:
- DNS / TCP latency in the hot path.
- Supply-chain risks from runtime network fetches.
- Missing-package failures during request processing.

---

## Plugin Config Object

The `config` parameter passed to `nginx.use()` becomes the second argument to
the plugin's main function.  It is a plain JS object:

```javascript
// Caller:
nginx.use('/opt/plugins/ratelimit', {zone: 'default', rate: '100r/s'});

// Plugin:
module.exports = function(nginx, config) {
    console.log(config.zone);  // 'default'
    console.log(config.rate);  // '100r/s'
};
```

The config object is stored in the plugin table as a `JSValue` and is accessible
read-only via `nginx.plugins[i].config`.

---

## nginx.install — Embedded Plugin Registration

`nginx.install(fn, config)` is the programmatic equivalent of `nginx.use()` for
inline plugin code (no file system):

```javascript
nginx.install(function(nginx, config) {
    var loc = nginx.http.servers[0].locations.find(l => l.path === '/metrics/');
    loc.handler = function(req) {
        req.respond(200, {}, collectMetrics());
    };
}, {});
```

It is used in the admin-shell and admin_UI_demo_v2 apps to self-register.

---

## nginx.plugins Introspection

```javascript
nginx.plugins.forEach(function(p) {
    console.log(p.path, JSON.stringify(p.config));
});
```

Exposed as an array property on the `nginx` COM object.  Each element:
```
{
  path:   '/opt/plugins/my-plugin',
  config: {logLevel: 'debug', upstream: 'backend'}
}
```

Used by the admin shell's JSON-RPC `plugins.list()` method.
