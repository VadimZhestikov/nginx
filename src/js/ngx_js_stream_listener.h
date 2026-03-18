
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_stream_listener.h — NginxStreamListener and NginxStreamServer:
 * bind a NginxSocket into nginx's stream (TCP/UDP) connection pipeline via
 * nginx.stream.attach(sock) / listener.addServer(srv).
 *
 * Stage 52 Phase G.
 */

#ifndef _NGX_JS_STREAM_LISTENER_H_INCLUDED_
#define _NGX_JS_STREAM_LISTENER_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>
#include <quickjs.h>
#include "ngx_js_socket.h"


#define NGX_JS_STREAM_LISTENER_REG_MAX      32
#define NGX_JS_STREAM_LISTENER_VSERVERS_MAX 16


/*
 * State for one JS-managed stream listener.
 *
 * Allocated with ngx_alloc() before fork; COW-shared across workers.
 * Fields are filled in phases:
 *   attach()    — fills socket_handle, sockaddr, port/addr routing structs
 *   addServer() — fills default_server, activates (pushes into cycle->listening)
 */
typedef struct {
    /* Back-reference to the NginxSocket */
    uint32_t                      socket_handle;

    /* Stream routing structures */
    ngx_stream_port_t             port;    /* .addrs → &addr, .naddrs = 1 */
    ngx_stream_in_addr_t          addr;    /* single IPv4 addr entry */

    /* Reusable sockaddr (pointed to by ls->sockaddr after activate) */
    struct sockaddr_in            sin;

    /* Display string — pointed to by ls->addr_text.data */
    u_char                        addr_text_buf[NGX_INET_ADDRSTRLEN + 8];
    size_t                        addr_text_len;

    /* Set by addServer() */
    ngx_stream_core_srv_conf_t   *default_server;   /* NULL until addServer() */
    ngx_cycle_t                  *cycle;             /* cycle used for activation */

    /* Set by addVirtualServer() — SNI routing */
    ngx_stream_core_srv_conf_t   *vservers[NGX_JS_STREAM_LISTENER_VSERVERS_MAX];
    ngx_uint_t                    nvservers;

    unsigned                      activated:1;       /* 1 after cycle->listening push */
} ngx_js_stream_listener_state_t;


/*
 * Global stream listener registry.
 * Allocated in master before fork; COW-shared with workers.
 */
extern ngx_js_stream_listener_state_t
    *ngx_js_stream_listener_reg[NGX_JS_STREAM_LISTENER_REG_MAX];


/*
 * Register NginxStreamServer and NginxStreamListener classes in a JSRuntime.
 * Called from ngx_js_com_register_classes().
 */
ngx_int_t  ngx_js_stream_listener_register_classes(JSRuntime *rt);

/*
 * Install shared prototypes in a JSContext.
 * Called from ngx_js_com_install_protos().
 */
ngx_int_t  ngx_js_stream_listener_install_protos(JSContext *ctx);

/*
 * Build the nginx.stream object (servers[] + attach()) and attach it to
 * nginx_obj.  Called from ngx_js_com_init().
 */
ngx_int_t  ngx_js_stream_install(JSContext *ctx, JSValue nginx_obj,
    ngx_cycle_t *cycle);

/*
 * Extract the ngx_stream_core_srv_conf_t* from a NginxStreamServer JS value.
 * Returns NULL if val is not a NginxStreamServer.
 */
ngx_stream_core_srv_conf_t *ngx_js_stream_server_get_cscf(JSValueConst srv,
    ngx_cycle_t **cycle_out);

/*
 * Wrap a stream listener registry slot in a JS NginxStreamListener object.
 * F2 — called from ngx_js_socket.c for NginxSocket.listener getter.
 */
JSValue  ngx_js_wrap_stream_listener(JSContext *ctx, uint32_t handle);


#endif /* _NGX_JS_STREAM_LISTENER_H_INCLUDED_ */
