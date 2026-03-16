
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_socket.h — NginxSocket: a bound+listening TCP socket
 * created via nginx.createSocket('host:port').
 *
 * Stage 52 — Phase A: pre-fork socket creation + JS wrapper.
 */

#ifndef _NGX_JS_SOCKET_H_INCLUDED_
#define _NGX_JS_SOCKET_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


#define NGX_JS_SOCKET_REG_MAX  32


/*
 * State for one JS-managed socket.
 *
 * Allocated with ngx_alloc() before fork so the block lives on the heap
 * and is copy-on-write shared across all worker processes at the same
 * virtual address.  Workers only ever read these fields.
 */
typedef struct {
    int       fd;        /* OS socket fd (bound + listening)         */
    uint16_t  port;      /* port in host byte order                  */
    char      addr[64];  /* display string, e.g. "127.0.0.1:9000"   */
} ngx_js_socket_state_t;


/*
 * Global socket registry.  Populated in the master process before fork;
 * COW-shared with all workers.  Slot index == NginxSocket handle.
 * NULL entries are unused.
 */
extern ngx_js_socket_state_t *ngx_js_socket_reg[NGX_JS_SOCKET_REG_MAX];


/*
 * Register the NginxSocket class definition in a JSRuntime.
 * Called from ngx_js_com_register_classes().
 */
ngx_int_t  ngx_js_socket_register_class(JSRuntime *rt);

/*
 * Install the shared NginxSocket prototype in a JSContext.
 * Called from ngx_js_com_install_protos().
 */
ngx_int_t  ngx_js_socket_install_proto(JSContext *ctx);

/*
 * Install nginx.createSocket() into the nginx_obj JS object.
 * Called from ngx_js_com_init() after nginx_obj is created.
 */
ngx_int_t  ngx_js_socket_install(JSContext *ctx, JSValue nginx_obj);

/*
 * Extract the socket handle (registry index) from a NginxSocket JS value.
 * Returns NGX_JS_SOCKET_REG_MAX on failure (wrong class or invalid handle).
 */
uint32_t   ngx_js_socket_get_handle(JSValueConst sock);


#endif /* _NGX_JS_SOCKET_H_INCLUDED_ */
