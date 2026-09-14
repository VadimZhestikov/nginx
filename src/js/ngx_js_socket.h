
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


/* ------------------------------------------------------------------------- *
 * COMCON M-LIB `allowHosts` — the OUTBOUND capability.
 * ------------------------------------------------------------------------- *
 * It lives in this file, not its own, for a build reason worth stating: adding
 * a source to src/js/config means re-running auto/configure for all four build
 * directories, two of which are hand-made sanitizer trees.  And it belongs near
 * the socket capability on merit as well -- the mask, budget and lifetime
 * plumbing an outbound wrapper needs is the same plumbing, and one copy of it is
 * the point.
 *
 * WHAT THIS CAPABILITY IS, AND WHAT IT DELIBERATELY IS NOT.  A confined fragment
 * is invoked SYNCHRONOUSLY: JS_Call, then JSON-stringify the result.  There is no
 * promise detection and no pending-job drain, so a capability that performs
 * network I/O cannot be handed to a fragment without making fragment invocation
 * asynchronous -- which would touch the F6/F12 deadline and the F2 per-invocation
 * memory allowance on the most safety-critical path in the system.  That is its
 * own increment and it is not this one.
 *
 * So this capability RECORDS INTENT and the host performs the I/O, which is the
 * pattern M-CFG already established for config: the tenant proposes what it
 * cannot apply.  `request()` is synchronous, checks the destination against the
 * `allowHosts` glob IN THE COMPARTMENT, and appends a descriptor; the host reads
 * the queue afterwards and decides.  The mediation therefore bites where the
 * capability is exercised rather than validating data after the fact, which is
 * what makes `allowHosts` an attenuation of authority and not a filter.
 */
#define NGX_JS_OUTBOUND_REG_MAX   64
#define NGX_JS_OUTBOUND_MAX_REC   32
#define NGX_JS_OUTBOUND_URL_LEN   256
#define NGX_JS_OUTBOUND_GLOB_LEN  128

extern JSClassID  ngx_js_outbound_class_id;

ngx_int_t  ngx_js_outbound_register_class(JSRuntime *rt);
ngx_int_t  ngx_js_outbound_install_proto(JSContext *ctx);
ngx_int_t  ngx_js_outbound_install(JSContext *ctx, JSValue nginx_obj);

/* -1 when val is not an outbound capability (mirrors ngx_js_socket_handle) */
int32_t    ngx_js_outbound_handle(JSValueConst val);

/* Re-wrap a host outbound cap compartment-native, attenuated by a host glob and
 * optionally by a `uses` budget and a `ttl` lifetime. */
JSValue    ngx_js_outbound_wrap(JSContext *ctx, uint32_t handle,
               const char *glob, size_t glob_len, const char *budget_key,
               uint32_t budget_limit, uint32_t budget_window,
               uint32_t ttl_seconds);

/* Host-glob match, shared with the route facet so the two cannot drift:
 *   "*"            matches everything
 *   "*.suffix"     leading star   — any host ending in ".suffix"
 *   "prefix*"      trailing star  — any host beginning "prefix"
 *   otherwise      exact
 */
ngx_int_t  ngx_js_glob_match(const u_char *glob, size_t glob_len,
               const u_char *s, size_t s_len);

/* M-LIB `window` — is a capability CLOSED right now?  `days` is a 7-bit mask
 * (bit 0 = Sunday), `from`/`to` are minutes since midnight UTC.  days == 0 means
 * no window.  from == to means the whole of an allowed day; from > to wraps past
 * midnight.  UTC deliberately: a gate that depends on the host TZ cannot be
 * tested identically on two machines and shifts under daylight saving without
 * anything being edited. */
ngx_int_t  ngx_js_window_closed(uint32_t days, uint32_t from, uint32_t to);

/* Apply a window to an already-wrapped capability.  A setter rather than four
 * more parameters on each wrap function: the window is optional, the wrap
 * signatures are already long, and threading it through every call site would
 * make three of them say `0, 0, 0` forever. */
void  ngx_js_socket_set_window(JSValueConst obj, uint32_t days, uint32_t from,
          uint32_t to);
void  ngx_js_outbound_set_window(JSValueConst obj, uint32_t days,
          uint32_t from, uint32_t to);

/* M-LIB `cosign`: apply a two-person rule to an already-wrapped capability.
 * `as` is the principal this wrapper ACTS FOR -- written on the trusted side as
 * the capability crosses, never reachable from inside the compartment, which is
 * what makes one wrapper exactly one vote. quorum == 0 leaves it uncosigned. */
void  ngx_js_socket_set_cosign(JSValueConst obj, const char *key,
          const char *as, uint32_t quorum, uint32_t within);
void  ngx_js_outbound_set_cosign(JSValueConst obj, const char *key,
          const char *as, uint32_t quorum, uint32_t within);


#endif /* _NGX_JS_SOCKET_H_INCLUDED_ */
