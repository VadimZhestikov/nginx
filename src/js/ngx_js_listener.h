
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_listener.h — NginxHttpListener: an HTTP pipeline binding
 * created via nginx.http.attach(sock).
 *
 * Stage 52 Phase B.
 *
 * An NginxHttpListener wraps a NginxSocket and the routing structures
 * needed to wire it into nginx's HTTP connection pipeline.  The OS socket
 * is already bound and listening; this object represents the HTTP-layer
 * configuration (virtual host routing, default server, etc.).
 *
 * The ngx_listening_t entry is NOT pushed into cycle->listening until
 * listener.addServer() sets a valid default_server (Phase C).  Until then
 * the socket accepts OS-level TCP connections but nginx workers do not
 * process them.
 */

#ifndef _NGX_JS_LISTENER_H_INCLUDED_
#define _NGX_JS_LISTENER_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_http.h>
#include <quickjs.h>
#include "ngx_js_socket.h"


#define NGX_JS_LISTENER_REG_MAX      32
#define NGX_JS_LISTENER_VSERVERS_MAX 32
/* NGX_JS_ACCEPT_HANDLERS_MAX and NGX_JS_L4_FILTERS_MAX are defined in ngx_js.h */


/*
 * State for one JS-managed HTTP listener.
 *
 * Allocated with ngx_alloc() before fork; COW-shared across workers.
 * Fields are filled in phases:
 *   attach()           — fills fd, sockaddr, port/addr routing structs
 *   addServer()        — fills default_server, activates (Phase C)
 *   addVirtualServer() — extends vservers[], rebuilds virtual_names (Phase D)
 */
typedef struct {
    /* Back-reference to the socket */
    uint32_t                  socket_handle;

    /* HTTP routing structures */
    ngx_http_port_t           port;          /* contains addrs pointer */
    ngx_http_in_addr_t        addr;          /* single IPv4 addr entry */

    /* Reusable sockaddr (pointed to by ls->sockaddr after activate) */
    struct sockaddr_in        sin;

    /* Display string — pointed to by ls->addr_text.data after activate */
    u_char                    addr_text_buf[NGX_INET_ADDRSTRLEN + 8];
    size_t                    addr_text_len;

    /* Set by addServer() (Phase C) */
    ngx_http_core_srv_conf_t *default_server;  /* NULL until addServer() */

    /* Virtual servers — set by addVirtualServer() (Phase D) */
    ngx_http_core_srv_conf_t *vservers[NGX_JS_LISTENER_VSERVERS_MAX];
    ngx_uint_t                nvservers;

    unsigned                  activated:1;     /* 1 after cycle->listening push */
    unsigned                  paused:1;        /* 1 if accept event removed in
                                                  this worker (soft removeListener);
                                                  per-worker via COW */
    unsigned                  closed:1;        /* 1 after hard removeListener */

    /* Accept hooks — JS-Pilgrim P4 */
    uint32_t                  accept_handlers[NGX_JS_ACCEPT_HANDLERS_MAX];
    ngx_uint_t                n_accept_handlers;
    ngx_listening_t          *ls;                 /* back-pointer, set on activate */

    /* L4 inbound filters — JS-Pilgrim P6/P12 */
    uint32_t                  l4_filters[NGX_JS_L4_FILTERS_MAX];
    ngx_uint_t                n_l4_filters;

    /* L4 send filters — JS-Pilgrim P13 */
    uint32_t                  l4_send_filters[NGX_JS_L4_FILTERS_MAX];
    ngx_uint_t                n_l4_send_filters;
} ngx_js_http_listener_state_t;


/*
 * Global listener registry — parallel to ngx_js_socket_reg.
 * Allocated in master before fork; COW-shared with workers.
 */
extern ngx_js_http_listener_state_t *ngx_js_listener_reg[NGX_JS_LISTENER_REG_MAX];


/*
 * Register the NginxHttpListener class in a JSRuntime.
 * Called from ngx_js_com_register_classes().
 */
ngx_int_t  ngx_js_listener_register_class(JSRuntime *rt);

/*
 * Install the shared NginxHttpListener prototype in a JSContext.
 * Called from ngx_js_com_install_protos().
 */
ngx_int_t  ngx_js_listener_install_proto(JSContext *ctx);

/*
 * Install nginx.http.attach() into the http_obj JS object.
 * Called from ngx_js_http_com_install() in ngx_js_com_http.c.
 */
ngx_int_t  ngx_js_listener_install(JSContext *ctx, JSValue http_obj);

/*
 * Push the pre-filled ngx_listening_t into cycle->listening.
 * Sets pool_size and log fields from the default_server.
 * Must be called after addServer() sets default_server.
 * Only valid in the master process (before fork).
 */
ngx_int_t  ngx_js_listener_activate(ngx_js_http_listener_state_t *st,
    ngx_cycle_t *cycle);

/*
 * Extract the ngx_http_core_srv_conf_t* from a NginxServer JS object.
 * Returns NULL if val is not a NginxServer.
 * If cycle_out is non-NULL, *cycle_out receives the server's cycle pointer.
 * Stage 52 Phase C — implemented in ngx_js_com_http.c.
 */
ngx_http_core_srv_conf_t *ngx_js_server_get_cscf(JSValueConst srv,
    ngx_cycle_t **cycle_out);

/*
 * Wrap a listener registry slot in a JS NginxHttpListener object.
 * F2 — called from ngx_js_socket.c for NginxSocket.listener getter.
 */
JSValue  ngx_js_wrap_listener(JSContext *ctx, uint32_t handle);

/*
 * P12: Install the __ngx_l4_make_source__ async-iterable factory into the
 * global object of ctx.  Called from ngx_js_com_init().
 */
ngx_int_t  ngx_js_l4_install_source_factory(JSContext *ctx);

/*
 * P17: Register a JS function in the accept-hook registry; return its index.
 * Non-static so that server.on('accept', fn) in ngx_js_com_http.c can use it.
 */
uint32_t  ngx_js_accept_hook_register_fn(JSContext *ctx, JSValueConst fn);

/*
 * P17: Register a JS function in the L4 filter registry; return its index.
 * Non-static so that server.addL4Filter(fn) in ngx_js_com_http.c can use it.
 */
uint32_t  ngx_js_l4_filter_register_fn(JSContext *ctx, JSValueConst fn);

/*
 * P17: After all JS scripts have been evaluated in init_conf, iterate
 * cycle->listening and override ls->handler to ngx_js_srv_accept_handler
 * on every standard HTTP socket whose default server has JS accept hooks
 * or L4 filters registered via server.on() / server.addL4Filter().
 */
void  ngx_js_srv_install_accept_hooks(ngx_cycle_t *cycle);


#endif /* _NGX_JS_LISTENER_H_INCLUDED_ */
