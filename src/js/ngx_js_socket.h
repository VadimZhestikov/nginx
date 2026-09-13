
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
#include "ngx_js_compartment.h"


#define NGX_JS_SOCKET_REG_MAX  32


/*
 * State for one JS-managed socket.
 *
 * Allocated with ngx_alloc() before fork so the block lives on the heap
 * and is copy-on-write shared across all worker processes at the same
 * virtual address.  Workers only ever read these fields.
 *
 * in_listening: set by ngx_js_listener_activate() when the socket fd is
 *   pushed into cycle->listening.  Prevents sock.close() from closing a
 *   socket already being used by nginx workers (Phase E).
 */
typedef struct ngx_js_socket_state_s {
    int                   fd;             /* OS socket fd (bound + listening) */
    uint16_t              port;           /* port in host byte order          */
    char                  addr[64];       /* display, e.g. "127.0.0.1:9000"   */
    unsigned              in_listening:1; /* 1 after addServer() activates fd  */
    ngx_js_compartment_t  owner;          /* COMCON A1.1: creating compartment */
} ngx_js_socket_state_t;


/*
 * Global socket registry.  Populated in the master process before fork;
 * COW-shared with all workers.  Slot index == NginxSocket handle.
 * NULL entries are unused.
 */
extern ngx_js_socket_state_t *ngx_js_socket_reg[NGX_JS_SOCKET_REG_MAX];

/*
 * Slot generations.  A registry index identifies a SLOT, not a socket: close()
 * frees the slot and the next createSocket() refills it.  Anything that keeps a
 * handle across that boundary -- a JS NginxSocket, or a listener's
 * socket_handle -- must remember the generation it was issued for and check it
 * on every use, or it silently comes to refer to a different socket.
 */
void ngx_js_socket_reg_install(uint32_t handle, ngx_js_socket_state_t *st);
uint32_t ngx_js_socket_gen_at(uint32_t handle);
ngx_js_socket_state_t *ngx_js_socket_state_checked(uint32_t handle,
    uint32_t gen);
JSValue ngx_js_socket_wrap_checked(JSContext *ctx, uint32_t handle,
    uint32_t gen);


/*
 * COMCON A1.1: the owning compartment of a registered socket, or HOST_ROOT for
 * an out-of-range / unregistered handle (conservative — an invalid handle is
 * then reachable only by HOST_ROOT). A listener's reach domain is its socket's,
 * so listener→socket/server edges gate on this rather than a separate field.
 */
ngx_js_compartment_t ngx_js_socket_owner(uint32_t handle);


/*
 * COMCON A2.1: the registry handle behind a NginxSocket JSValue, or -1 if the
 * value is not a socket. Lets the grant primitive re-wrap the same socket into
 * a tenant context by handle.
 */
int32_t ngx_js_socket_handle(JSValueConst val);


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

/*
 * Wrap a registry slot handle in a JS NginxSocket object.
 * F2 — called from ngx_js_listener.c and ngx_js_stream_listener.c for
 * the listener.socket cross-reference getter.
 */
JSValue    ngx_js_socket_wrap(JSContext *ctx, uint32_t handle);

/* full authority over every field — the default for a non-mediated wrapper */
#define NGX_JS_SOCKET_MASK_ALL  0xffffffffu

/*
 * COMCON mediate: wrap a socket with a field mask (bit index == getter magic:
 * 0 address, 1 port, 2 fd, 3 listener). A clear bit hides that field (reads
 * undefined) — an attenuation-only membrane over the granted cap.
 */
/* M-LIB `ttl`: wrap with a lifetime in seconds (0 = no expiry), plus a budget */
JSValue    ngx_js_socket_wrap_bounded(JSContext *ctx, uint32_t handle,
    uint32_t mask, const char *budget_key, uint32_t budget_limit,
    uint32_t budget_window, uint32_t ttl_seconds);
/* M-LIB `uses`: wrap with a named fleet-wide budget (limit 0 = unbudgeted) */
JSValue    ngx_js_socket_wrap_budgeted(JSContext *ctx, uint32_t handle,
    uint32_t mask, const char *budget_key, uint32_t budget_limit,
    uint32_t budget_window);
JSValue    ngx_js_socket_wrap_masked(JSContext *ctx, uint32_t handle,
               uint32_t mask);


#endif /* _NGX_JS_SOCKET_H_INCLUDED_ */
