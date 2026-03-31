# 08 — L4 Raw-TCP Pipeline (P4 / P6 / P13)

## Overview

Below the HTTP protocol layer, JS_Pilgrim can intercept, transform, and generate
raw TCP byte streams.  Three hooks operate at this level:

| Step | API | Direction | Trigger |
|---|---|---|---|
| P4 | `listener.on('accept', fn)` | metadata only | new TCP connection accepted |
| P6 | `listener.addL4Filter(fn)` | client → nginx (inbound) | bytes arriving from client |
| P13 | `listener.addL4SendFilter(fn)` | nginx → client (outbound) | bytes sent to client |

---

## NginxSocket and NginxHttpListener

### NginxSocket

`nginx.createSocket(host, port)` creates a raw TCP socket (bind + listen) managed
entirely by JS code.  The socket is registered in the global `ngx_js_socket_reg[32]`
array before fork so the fd is inherited by all workers.

```c
typedef struct {
    int       fd;
    uint16_t  port;
    char      addr[64];
    unsigned  in_listening:1;
} ngx_js_socket_state_t;
```

### NginxHttpListener

`new NginxHttpListener(socketHandle)` wraps a socket in an HTTP listener, adding
L7 routing on top of the L4 socket.  The listener state is in
`ngx_js_http_listener_state_t`:

```c
typedef struct {
    uint32_t                      socket_handle;
    ngx_http_port_t               port;
    ngx_http_in_addr_t            addr;
    struct sockaddr_in            sin;
    ngx_http_core_srv_conf_t     *default_server;
    ngx_http_core_srv_conf_t     *vservers[32];
    ngx_uint_t                    nvservers;
    uint32_t                      accept_handlers[8];
    ngx_uint_t                    n_accept_handlers;
    uint32_t                      l4_filters[8];
    ngx_uint_t                    n_l4_filters;
    uint32_t                      l4_send_filters[8];
    ngx_uint_t                    n_l4_send_filters;
} ngx_js_http_listener_state_t;
```

---

## P4: Accept Hooks

```javascript
listener.on('accept', function(conn) {
    if (conn.remoteAddr.startsWith('192.168.')) {
        conn.close();
    }
});
```

Accept hooks fire synchronously when a new connection is accepted.  The `conn`
object wraps `ngx_connection_t`:
- `conn.remoteAddr` — client IP string
- `conn.remotePort` — client port number
- `conn.close()` — close the connection immediately

Accept hooks are invoked before any HTTP parsing.  They are useful for:
- IP-based allow-listing.
- Connection counting / rate limiting at the socket level.
- Logging raw connection events.

`n_accept_handlers` and `accept_handlers[8]` in the listener state store the
registered handler indices.  Handlers are called in registration order.

---

## P6: L4 Inbound Filter

```javascript
listener.addL4Filter(async function*(chunks, conn) {
    for await (const chunk of chunks) {
        // chunk is a Uint8Array / Buffer
        yield processInbound(chunk);
    }
});
```

The filter is an async generator.  `chunks` is an async iterable driven by the
nginx event loop.  The filter yields transformed chunks that are passed onward to
the HTTP protocol parser (or the next L4 filter if multiple are registered).

### Generator Loop (C side)

`ngx_js_l4_filter_loop` in `ngx_js_listener.c` drives the generator:

1. On `NGX_READ` event: calls `iterator.next(chunk)` to advance the generator.
2. The generator may `yield` immediately (synchronous path) or return a pending
   Promise (async path → suspend).
3. On suspend: `ngx_js_l4_pending_t` is stored in `w->l4_pending`.
4. On yield: the yielded value is written to the connection's send buffer.

### Connection Object

The `conn` parameter is an `NginxConnection` JS object wrapping the C
`ngx_connection_t *`.  It has:
- `conn.remoteAddr`, `conn.remotePort` — client identity
- `conn.localAddr`, `conn.localPort` — server bind address
- `conn.send(buf)` — write raw bytes to client
- `conn.close()` — close connection

---

## P13: L4 Send Filter

```javascript
listener.addL4SendFilter(async function*(chunks, conn) {
    for await (const chunk of chunks) {
        yield encrypt(chunk);
    }
});
```

Mirrors P6 but for the **outbound** direction (data flowing from nginx/backend to
the client).  The mechanics are identical: async generator, `ngx_js_l4_pending_t`,
same generator loop but attached to the write-filter chain rather than the read chain.

Send filters are useful for:
- Protocol wrapping (TLS-in-TLS, custom framing).
- Bandwidth throttling.
- Response logging at byte granularity.

---

## P17: Standard nginx listen Sockets

P4, P6, and P13 originally only applied to `NginxSocket`-created sockets.  P17
extends them to standard nginx `listen` directive sockets:

```nginx
# nginx.conf
server {
    listen 127.0.0.1:8080;
    location /api/ { }
}
```

```javascript
// init.js
nginx.http.servers[0].listener.on('accept', fn);
nginx.http.servers[0].listener.addL4Filter(generatorFn);
```

Implementation: `ngx_js_com_http.c` synthesises an `NginxHttpListener` wrapper
for each `ngx_http_addr_conf_t` and makes it available via `server.listener`.
The underlying accept handler is patched to call JS accept hooks.

---

## L4 Filter Composition

Multiple L4 filters on a listener stack:

```
Client bytes
  → filter[0]: strips custom framing
  → filter[1]: decrypts
  → HTTP parser
```

The C dispatcher wraps the generator return value of filter[i] as the `chunks`
iterable passed to filter[i+1], creating a chain of generator wrappings.

---

## Key Files

| File | Content |
|---|---|
| `ngx_js_listener.c` | NginxHttpListener, NginxConnection, L4 filter loop, accept dispatch |
| `ngx_js_listener.h` | Public declarations |
| `ngx_js_socket.c` | NginxSocket bind/listen, socket registry |
| `ngx_js_socket.h` | Public declarations |
| `ngx_js_stream_listener.c` | Stream (TCP) protocol listener variant |
