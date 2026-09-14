
/*
 * Copyright (C) nginx JS contributors
 *
 * Stage 52 — nginx.createSocket(addrPort)
 *
 * Creates a bound + listening TCP socket and wraps it in a NginxSocket
 * JS object.
 *
 * Phase A (pre-fork only): syscalls execute directly in the master process
 * during init_conf.  Phase F will add a manager-thread round-trip for
 * sockets created post-fork from worker request handlers.
 *
 * JS API:
 *
 *   const sock = nginx.createSocket('0.0.0.0:9000');
 *   sock.address  // "0.0.0.0:9000"
 *   sock.port     // 9000  (number)
 *   sock.fd       // OS file-descriptor number
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <quickjs.h>
#include <cutils.h>
#include "ngx_js_com.h"
#include "ngx_js.h"
#include "ngx_js_socket.h"
#include "ngx_js_listener.h"
#include "ngx_js_stream_listener.h"
#include "ngx_js_sw.h"


JSClassID              ngx_js_socket_class_id;
ngx_js_socket_state_t *ngx_js_socket_reg[NGX_JS_SOCKET_REG_MAX];

/*
 * Per-slot generation counter, bumped every time a slot is handed out.
 *
 * A NginxSocket holds an INDEX into the registry, and close() frees the slot
 * for reuse while the JS object keeps its index.  Without a generation, the
 * next createSocket() handed the freed slot back and every stale handle became
 * a live handle to an unrelated socket: a closed `a` then read b's address and
 * fd, and a.close() destroyed b's listening socket.  The generation makes an
 * index alone insufficient -- a handle must also match the incarnation it was
 * issued for.  It lives beside the registry, not inside the state, because the
 * state is freed on close and the generation has to outlive it.
 */
static uint32_t        ngx_js_socket_gen[NGX_JS_SOCKET_REG_MAX];


ngx_js_compartment_t
ngx_js_socket_owner(uint32_t handle)
{
    if (handle >= NGX_JS_SOCKET_REG_MAX || ngx_js_socket_reg[handle] == NULL) {
        return NGX_JS_COMPARTMENT_HOST_ROOT;
    }

    return ngx_js_socket_reg[handle]->owner;
}


/*
 * The generation currently occupying a slot, for a holder that wants to record
 * which incarnation it attached to (a listener does).  Returns 0 for a slot
 * that is out of range or empty; a live slot's generation is always >= 1,
 * since createSocket() bumps before storing.
 */
uint32_t
ngx_js_socket_gen_at(uint32_t handle)
{
    if (handle >= NGX_JS_SOCKET_REG_MAX || ngx_js_socket_reg[handle] == NULL) {
        return 0;
    }

    return ngx_js_socket_gen[handle];
}


/*
 * The ONLY way a socket enters the registry.  Bumping the generation is what
 * retires every handle issued for the slot's previous occupant, so it must
 * happen on every install -- not just the one in createSocket().  It was a
 * rule to remember, and the broadcast receive path in ngx_js_module.c did not:
 * it stored straight into ngx_js_socket_reg[], leaving the generation where the
 * closed socket had left it, so a stale handle from before the close matched
 * again and resolved to the socket that arrived over SCM_RIGHTS.  Making the
 * bump structural is the point of this function; do not assign to the registry
 * anywhere else.
 */
void
ngx_js_socket_reg_install(uint32_t handle, ngx_js_socket_state_t *st)
{
    if (handle >= NGX_JS_SOCKET_REG_MAX) {
        return;
    }

    ngx_js_socket_gen[handle]++;
    ngx_js_socket_reg[handle] = st;
}


/*
 * Resolve a handle REMEMBERED by a long-lived holder.  Same rule as the JS
 * handle path: an index is not enough, the incarnation has to match too.
 */
ngx_js_socket_state_t *
ngx_js_socket_state_checked(uint32_t handle, uint32_t gen)
{
    if (handle >= NGX_JS_SOCKET_REG_MAX || ngx_js_socket_reg[handle] == NULL) {
        return NULL;
    }

    if (gen != 0 && ngx_js_socket_gen[handle] != gen) {
        return NULL;
    }

    return ngx_js_socket_reg[handle];
}


/* ------------------------------------------------------------------ */
/* NginxSocket opaque + finalizer                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t  handle;   /* index into ngx_js_socket_reg[] */
    uint32_t  gen;      /* incarnation this handle was issued for */
    uint32_t  mask;     /* COMCON mediate: allowed fields, bit==magic (see get) */
    /*
     * COMCON M-LIB `uses`: a fleet-wide budget on this wrapper. limit == 0 means
     * unbudgeted, which is every capability that was not mediated with uses().
     * The key is the operator's name for the counter, so two capabilities can
     * share one budget (or not) by naming.
     */
    uint32_t  budget_limit;
    uint32_t  budget_window;   /* seconds; the window is FIXED, not sliding */
    char      budget_key[64];
    /*
     * COMCON M-LIB `ttl`: an absolute expiry for this capability, in ngx_time()
     * seconds; 0 = never. The clock starts when the capability CROSSES into the
     * compartment, not when mediate() built the descriptor -- the descriptor is
     * data and carries only a duration, so there is one clock (nginx's) rather
     * than two that could disagree.
     */
    time_t    expires;
    /*
     * M-LIB `window`: a RECURRING lifetime beside `ttl`'s absolute one.  days ==
     * 0 means no window, which is every capability that was not mediated with
     * window(); the two compose, and a capability can be both expiring and
     * scheduled.
     */
    uint32_t  win_days;
    uint32_t  win_from;
    uint32_t  win_to;
    /*
     * M-LIB `cosign`: the two-person rule.  quorum == 0 means uncosigned.
     * `cosign_as` is the principal this wrapper ACTS FOR, written on the trusted
     * side when the capability crosses; there is no path from inside the
     * compartment that sets it, which is what makes one wrapper one vote.
     */
    uint32_t  cosign_quorum;
    uint32_t  cosign_within;   /* seconds; anchored at the FIRST consent */
    char      cosign_key[64];
    char      cosign_as[48];
    /*
     * M-LIB `protocol`: a session type over this capability's operations.
     * proto_n == 0 means unsequenced.  Each term is (op id << 1) | starred, and
     * `proto_pos` is the cursor -- PER WRAPPER, deliberately not fleet-wide the
     * way a cosign record is: a session type describes ONE conversation, and two
     * holders sharing a cursor would interleave into nonsense.
     */
    uint8_t   proto_term[NGX_JS_PROTO_MAX];
    uint8_t   proto_n;
    uint8_t   proto_pos;
    /*
     * WHICH FRAGMENT THIS WRAPPER WAS GRANTED TO; 0 = the host's own, always
     * usable.  Checked before every other gate: a capability that is not yours
     * is not yours redacted, budgeted or scheduled -- it is not yours at all.
     */
    uint32_t  owner;
} ngx_js_socket_opaque_t;


/*
 * The one way to turn a JS handle into state.  Returns NULL for an index that
 * is out of range, freed, or -- the case a bare NULL check misses -- refilled
 * by a later createSocket() since this handle was issued.
 */
static ngx_js_socket_state_t *
ngx_js_socket_state_of(ngx_js_socket_opaque_t *op)
{
    if (op == NULL || op->handle >= NGX_JS_SOCKET_REG_MAX) {
        return NULL;
    }

    if (ngx_js_socket_reg[op->handle] == NULL
        || ngx_js_socket_gen[op->handle] != op->gen)
    {
        return NULL;
    }

    return ngx_js_socket_reg[op->handle];
}


int32_t
ngx_js_socket_handle(JSValueConst val)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_socket_class_id);
    if (op == NULL) {
        return -1;
    }

    /* dead once the slot has moved on, same as every other handle path */
    if (ngx_js_socket_state_of(op) == NULL) {
        return -1;
    }

    return (int32_t) op->handle;
}


static void
ngx_js_socket_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_socket_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef  ngx_js_socket_class = {
    "NginxSocket",
    .finalizer = ngx_js_socket_finalizer,
};


/* ------------------------------------------------------------------ */
/* NginxSocket property getters                                         */
/* magic: 0=address, 1=port, 2=fd, 3=listener                         */
/* ------------------------------------------------------------------ */

/*
 * Spend one use. Denial goes through the SAME gate machinery as every other
 * reach denial, which is what makes audit mode work here for free: in audit the
 * event is logged and the operation ALLOWED, so an operator can watch a budget
 * be exceeded before switching it on -- the audit-first rollout, applied to
 * rate limits rather than re-invented for them.
 */
/* the address this wrapper points at, for the denial record's `obj` field */
static const char *
st_addr_of(ngx_js_socket_opaque_t *op)
{
    ngx_js_socket_state_t  *st = ngx_js_socket_state_of(op);

    return (st != NULL) ? st->addr : "-";
}


static ngx_int_t
ngx_js_socket_budget_spend(JSContext *ctx, ngx_js_socket_opaque_t *op)
{
    if (ngx_js_shared_budget_charge(ctx, op->budget_key, op->budget_limit,
                                    op->budget_window)
        == NGX_OK)
    {
        return NGX_OK;
    }

    /* returns 0 in audit mode (log-and-allow), 1 when the gate should deny */
    if (ngx_js_compartment_denial(NGX_JS_DENIAL_BUDGET_USES, op->budget_key)) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


/*
 * M-LIB `cosign`: the two-person gate, in ONE place for both capability kinds.
 *
 * Returns NGX_OK when the quorum is met and the operation may proceed.  A
 * shortfall goes through the same denial machinery as every other gate, so audit
 * mode works here for free -- and audit mode is unusually interesting for this
 * word: an operator can watch which operations WOULD have needed a second
 * signature before switching the rule on.
 *
 * The consent is recorded even when the gate denies.  That is the feature, not a
 * leak: the first operator's attempt IS their signature, and the second
 * operator's identical attempt is what executes it.
 */
static ngx_int_t
ngx_js_cosign_gate(JSContext *ctx, const char *key, const char *as,
    uint32_t quorum, uint32_t within, const char *obj)
{
    if (ngx_js_shared_cosign_record(ctx, key, as, quorum, within) == NGX_OK) {
        return NGX_OK;
    }

    /* returns 0 in audit mode (log-and-allow), 1 when the gate should deny */
    if (ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_COSIGN, obj)) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


/*
 * M-LIB `protocol`: the transition, in ONE place for both capability kinds.
 *
 * The grammar is deliberately tiny -- a sequence of DISTINCT operation names,
 * each required once or starred for any number -- because that is what makes
 * greedy matching unambiguous and the whole state a cursor.  A term that does not
 * match may be SKIPPED only if it is starred (a starred step may happen zero
 * times); a required step that has not happened yet is a violation, and so is any
 * operation at all once the cursor has run off the end.
 *
 * Returns the NEW cursor, or NGX_ERROR for a violation.  It does not write the
 * cursor back: see the call sites, where the decision is separated from the
 * effect so that an operation another gate still refuses does not advance the
 * conversation.
 */
static ngx_int_t
ngx_js_protocol_step(const uint8_t *term, ngx_uint_t n, ngx_uint_t pos,
    ngx_uint_t opid)
{
    while (pos < n) {
        if ((ngx_uint_t) (term[pos] >> 1) == opid) {
            /* a starred term stays under the cursor; a required one is consumed */
            return (term[pos] & 1) ? (ngx_int_t) pos : (ngx_int_t) (pos + 1);
        }

        if (!(term[pos] & 1)) {
            return NGX_ERROR;          /* a required step has not happened yet */
        }

        pos++;                         /* a starred step may match zero times */
    }

    return NGX_ERROR;                  /* past the end: the conversation is over */
}


/*
 * The operation namespaces, one per capability kind.  The socket ids are the
 * getter's own `magic` values, so the gate needs no second mapping -- two
 * numberings of the same four operations is how a protocol ends up enforcing an
 * order over the wrong fields.
 */
ngx_int_t
ngx_js_socket_op_id(const char *name)
{
    static const char  *ops[] = { "address", "port", "fd", "listener" };
    ngx_uint_t          i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        if (ngx_strcmp(name, ops[i]) == 0) {
            return (ngx_int_t) i;
        }
    }

    return NGX_ERROR;
}


/*
 * The outbound namespace has ONE member, and that is a statement rather than an
 * omission: `pending` and `clear` are the HOST's half of this capability and are
 * reach-gated (out.drain), so a fragment can never perform them and they are not
 * part of the conversation a fragment can have.  Listing them would let an
 * operator write a protocol that can never advance.
 *
 * So on an outbound capability `protocol('request')` means ONE outbound intent,
 * ever -- which is a real attenuation and a different one from uses(1): a budget
 * is fleet-wide and resets with its window, a protocol is per-wrapper and never
 * resets.
 */
ngx_int_t
ngx_js_outbound_op_id(const char *name)
{
    return (ngx_strcmp(name, "request") == 0) ? 0 : NGX_ERROR;
}


static JSValue
ngx_js_socket_get(JSContext *ctx, JSValueConst this_val, int magic)
{
    ngx_js_socket_opaque_t  *op;
    ngx_js_socket_state_t   *st;
    ngx_int_t                pstep = NGX_ERROR;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_socket_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    /*
     * BEFORE THE MASK, BEFORE EVERYTHING: is this capability even this
     * fragment's?
     *
     * A job queued by fragment A and run during B's invocation holds A's
     * wrappers.  The drain (v5.93) keeps that from arising while A's
     * continuations fit its job budget; this keeps it from MATTERING when they
     * do not.  A capability that is not yours is not yours redacted, budgeted or
     * scheduled -- it is not yours at all, so this is the first question and the
     * only one whose answer does not depend on what the operator wrote.
     */
    if (ngx_js_cap_foreign(op->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_OWNER, st_addr_of(op)))
    {
        return JS_UNDEFINED;
    }

    /*
     * COMCON mediate: a redacted field (mask bit clear for this magic) reads as
     * undefined — the membrane hides it. Attenuation-only: a mask can only
     * remove authority a wrapper already had (A(cap′) ⊆ A(cap)).
     */
    if (magic >= 0 && magic < 32 && !(op->mask & (1u << magic))) {
        return JS_UNDEFINED;
    }

    /*
     * COMCON M-LIB `uses`: a USE is any gated operation on the capability -- a
     * read of a mediated field as much as a method call. Charging only calls
     * would make `s.address` free and let a tenant spend the interesting part
     * of a capability without touching its budget; charging everything is the
     * reading an operator can predict from the word "uses".
     *
     * Redacted reads are NOT charged: they happen above, before this point,
     * because a field the membrane hides was never an exercise of the
     * capability in the first place.
     */
    /*
     * `ttl`: an expired capability is refused BEFORE the budget is charged --
     * spending budget on an operation that cannot happen would make the audit
     * read as if the tenant were still working.
     */
    if (op->expires != 0 && ngx_time() >= op->expires
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_EXPIRED, st_addr_of(op)))
    {
        return JS_UNDEFINED;
    }

    /* Beside the expiry, and for the same reason it precedes the budget charge:
     * spending budget on an operation that cannot happen makes the audit read as
     * though the tenant were still working. */
    if (ngx_js_window_closed(op->win_days, op->win_from, op->win_to)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_WINDOW, st_addr_of(op)))
    {
        return JS_UNDEFINED;
    }

    /*
     * `protocol` is CHECKED here and COMMITTED at the bottom, and that split is
     * the one interesting thing about this gate.
     *
     * Every other gate's decision is also its effect: a budget charge happens
     * when it is decided, and a cosign consent IS the decision.  A protocol's
     * effect -- advancing the cursor -- can be deferred, and it MUST be, because
     * an operation that a later gate still refuses did not happen and must not
     * move the conversation on.  Checking after cosign would record a signature
     * for an operation about to be refused for being out of order; committing
     * before the budget would advance a conversation whose operation was never
     * performed.  So the check goes first and the commit goes last.
     *
     * A gate that mutates state has to separate its decision from its effect, or
     * it can only ever be last.
     */
    if (op->proto_n > 0) {
        pstep = ngx_js_protocol_step(op->proto_term, op->proto_n,
                                    op->proto_pos, (ngx_uint_t) magic);
        if (pstep == NGX_ERROR
            && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_PROTOCOL,
                                         st_addr_of(op)))
        {
            return JS_UNDEFINED;
        }
    }

    /*
     * `cosign` sits AFTER the expiry and the window and BEFORE the budget.
     *
     * After, because consent must not be bankable outside the hours the
     * capability is usable: a principal who could sign at 03:00 for an operation
     * the window forbids would have moved the decision out of the window the
     * operator wrote down.
     *
     * Before, for the reason `ttl` established -- an operation that cannot
     * happen must not spend budget, or the audit reads as though the tenant were
     * still working.
     */
    if (op->cosign_quorum > 0
        && ngx_js_cosign_gate(ctx, op->cosign_key, op->cosign_as,
                              op->cosign_quorum, op->cosign_within,
                              st_addr_of(op)) != NGX_OK)
    {
        return JS_UNDEFINED;
    }

    if (op->budget_limit > 0
        && ngx_js_socket_budget_spend(ctx, op) != NGX_OK)
    {
        return JS_UNDEFINED;
    }

    /* Every gate passed: the operation is happening, so the conversation moves.
     * In audit mode pstep may be NGX_ERROR and the operation was allowed anyway;
     * the cursor then stays where it was, because a violation that was logged
     * rather than denied is still not a legal transition. */
    if (op->proto_n > 0 && pstep != NGX_ERROR) {
        op->proto_pos = (uint8_t) pstep;
    }

    st = ngx_js_socket_state_of(op);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx, "NginxSocket: invalid handle");
    }

    switch (magic) {
    case 0:  return JS_NewString(ctx, st->addr);
    case 1:  return JS_NewInt32(ctx, (int32_t) st->port);
    case 2:  return JS_NewInt32(ctx, (int32_t) st->fd);

    case 3:  /* listener — NginxHttpListener | NginxStreamListener | null */
    {
        ngx_uint_t  i;

        /*
         * COMCON A1.1 (defense-in-depth): the listener edge is the entry to
         * the sock→listener→serverByName→server→addLocation reach cycle. A
         * compartment that was handed a socket it does not own cannot walk it.
         * No-op today (current compartment is HOST_ROOT); isolates once
         * confined fragments run.
         */
        if (!ngx_js_compartment_may_reach(st->owner)
            && ngx_js_compartment_denial(NGX_JS_DENIAL_SOCK_LISTENER,
                                         st->addr))
        {
            return JS_NULL;
        }

        for (i = 0; i < NGX_JS_LISTENER_REG_MAX; i++) {
            if (ngx_js_listener_reg[i] != NULL
                && ngx_js_listener_reg[i]->socket_handle == op->handle)
            {
                return ngx_js_wrap_listener(ctx, (uint32_t) i);
            }
        }

        for (i = 0; i < NGX_JS_STREAM_LISTENER_REG_MAX; i++) {
            if (ngx_js_stream_listener_reg[i] != NULL
                && ngx_js_stream_listener_reg[i]->socket_handle == op->handle)
            {
                return ngx_js_wrap_stream_listener(ctx, (uint32_t) i);
            }
        }

        return JS_NULL;
    }
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* sock.close() — Phase E (pre-fork) + F3 (worker)                    */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_socket_close(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_socket_opaque_t  *op;
    ngx_js_socket_state_t   *st;
    ngx_js_worker_t         *w;
    ngx_uint_t               i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_socket_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    st = ngx_js_socket_state_of(op);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx,
            "sock.close: socket already closed or invalid");
    }

    /* And the same first question the getter asks: a leftover continuation must
     * not close a socket on behalf of the fragment now running. */
    if (ngx_js_cap_foreign(op->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_OWNER, st->addr))
    {
        return JS_ThrowTypeError(ctx, "sock.close: denied (not this fragment's)");
    }

    /* COMCON SR-1 MEDIUM-4: close() destroys host state — a mutating op, not a
     * scalar read. A tenant handed this socket via grantToTenant may not close
     * a socket it does not own. */
    if (!ngx_js_compartment_may_reach(st->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_SOCK_MUTATE, st->addr))
    {
        return JS_ThrowTypeError(ctx, "sock.close: denied (not owner)");
    }

    if (st->in_listening) {
        return JS_ThrowInternalError(ctx,
            "sock.close: cannot close a socket already added to"
            " cycle->listening — call addServer() activates the socket"
            " for nginx workers");
    }

    /* Close the OS file descriptor */
    if (st->fd >= 0) {
        (void) close(st->fd);
        st->fd = -1;
    }

    /* Remove from global registry and free the state */
    ngx_js_socket_reg[op->handle] = NULL;
    ngx_free(st);

    /* F3: also remove from the worker-local registry if in a worker */
    if (ngx_process == NGX_PROCESS_WORKER) {
        w = JS_GetContextOpaque(ctx);
        if (w != NULL) {
            for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
                if (w->local_socket_reg[i] == st) {
                    w->local_socket_reg[i] = NULL;
                    break;
                }
            }
        }
    }

    return JS_UNDEFINED;
}


/* ------------------------------------------------------------------ */
/* sock.broadcast() — Phase F4: deliver socket fd to all other workers */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_socket_broadcast(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_socket_opaque_t  *op;
    ngx_js_socket_state_t   *st;
    int                      rc;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_socket_class_id);
    if (!op) {
        return JS_EXCEPTION;
    }

    st = ngx_js_socket_state_of(op);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx,
            "sock.broadcast: socket already closed or invalid");
    }

    if (ngx_process != NGX_PROCESS_WORKER) {
        return JS_ThrowInternalError(ctx,
            "sock.broadcast: only valid in worker processes");
    }

    if (ngx_js_cap_foreign(op->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_OWNER, st->addr))
    {
        return JS_ThrowTypeError(ctx,
            "sock.broadcast: denied (not this fragment's)");
    }

    /* COMCON SR-1 MEDIUM-4: broadcast distributes the fd fleet-wide — mutating;
     * gate on ownership like close(). */
    if (!ngx_js_compartment_may_reach(st->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_SOCK_MUTATE, st->addr))
    {
        return JS_ThrowTypeError(ctx, "sock.broadcast: denied (not owner)");
    }

    rc = ngx_js_socket_mgr_broadcast(op->handle, st->addr,
                                     ngx_strlen(st->addr));
    if (rc < 0) {
        return JS_ThrowInternalError(ctx,
            "sock.broadcast: manager failed to distribute socket to workers");
    }

    return JS_UNDEFINED;
}


static const JSCFunctionListEntry  ngx_js_socket_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("address",   ngx_js_socket_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("port",      ngx_js_socket_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("fd",        ngx_js_socket_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("listener",  ngx_js_socket_get, NULL, 3),
    JS_CFUNC_DEF(        "close",     0, ngx_js_socket_close),
    JS_CFUNC_DEF(        "broadcast", 0, ngx_js_socket_broadcast),
};


ngx_int_t
ngx_js_socket_register_class(JSRuntime *rt)
{
    return JS_NewClass(rt, ngx_js_socket_class_id, &ngx_js_socket_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_socket_install_proto(JSContext *ctx)
{
    JSValue  proto;

    proto = JS_NewObject(ctx);
    if (JS_IsException(proto)) {
        return NGX_ERROR;
    }

    JS_SetPropertyFunctionList(ctx, proto,
                               ngx_js_socket_proto_funcs,
                               countof(ngx_js_socket_proto_funcs));

    JS_SetClassProto(ctx, ngx_js_socket_class_id, proto);
    return NGX_OK;
}


/* ------------------------------------------------------------------ */
/* Wrap a registry slot in a JS NginxSocket object                     */
/* ------------------------------------------------------------------ */

JSValue
ngx_js_socket_wrap(JSContext *ctx, uint32_t handle)
{
    return ngx_js_socket_wrap_masked(ctx, handle, NGX_JS_SOCKET_MASK_ALL);
}


/*
 * COMCON mediate: wrap a socket with a field mask (bit index == getter magic:
 * 0 address, 1 port, 2 fd, 3 listener). A clear bit hides that field
 * (reads undefined). Attenuation-only — a membrane never adds authority.
 */
/*
 * Wrap a socket a holder recorded earlier, refusing if the slot has been
 * recycled since.  ngx_js_socket_wrap() stamps the handle with the generation
 * the slot has NOW, which is exactly right when the caller has just created or
 * been handed the socket -- and exactly wrong for a back-reference stored long
 * ago, because it would mint a valid handle to whatever moved in.  A retired
 * listener asked for its socket that way and got a live capability to an
 * unrelated one.
 */
JSValue
ngx_js_socket_wrap_checked(JSContext *ctx, uint32_t handle, uint32_t gen)
{
    if (ngx_js_socket_state_checked(handle, gen) == NULL) {
        return JS_NULL;
    }

    return ngx_js_socket_wrap(ctx, handle);
}


JSValue
ngx_js_socket_wrap_masked(JSContext *ctx, uint32_t handle, uint32_t mask)
{
    return ngx_js_socket_wrap_budgeted(ctx, handle, mask, NULL, 0, 0);
}


/*
 * COMCON M-LIB: the same wrapper, plus a `uses` budget. The budget travels on
 * the WRAPPER, not on the socket: two fragments granted the same socket under
 * different budgets get different wrappers, and neither can see or spend the
 * other's -- unless the operator names the same counter, which is how budgets
 * are shared on purpose.
 */
JSValue
ngx_js_socket_wrap_budgeted(JSContext *ctx, uint32_t handle, uint32_t mask,
    const char *budget_key, uint32_t budget_limit, uint32_t budget_window)
{
    return ngx_js_socket_wrap_bounded(ctx, handle, mask, budget_key,
                                      budget_limit, budget_window, 0);
}


/* the same wrapper, plus a `ttl` lifetime in seconds (0 = no expiry) */
JSValue
ngx_js_socket_wrap_bounded(JSContext *ctx, uint32_t handle, uint32_t mask,
    const char *budget_key, uint32_t budget_limit, uint32_t budget_window,
    uint32_t ttl_seconds)
{
    JSValue                  obj;
    ngx_js_socket_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_socket_opaque_t));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->handle = handle;
    op->gen = (handle < NGX_JS_SOCKET_REG_MAX) ? ngx_js_socket_gen[handle] : 0;
    op->mask = mask;

    if (budget_key != NULL && budget_limit > 0) {
        ngx_cpystrn((u_char *) op->budget_key, (u_char *) budget_key,
                    sizeof(op->budget_key));
        op->budget_limit = budget_limit;
        op->budget_window = budget_window ? budget_window : 1;
    }

    if (ttl_seconds > 0) {
        op->expires = ngx_time() + (time_t) ttl_seconds;
    }

    obj = JS_NewObjectClass(ctx, ngx_js_socket_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


/* ------------------------------------------------------------------ */
/* Address parser: "host:port" → host string + port number             */
/* ------------------------------------------------------------------ */

static int
ngx_js_parse_addr_port(const char *s, char *host_buf, size_t host_bufsz,
    uint16_t *port_out)
{
    const char  *colon;
    size_t       host_len;
    long         port;
    char        *endp;

    /* Use the last ':' so IPv6 literals like "[::1]:80" work if we
     * strip the brackets first — for Phase A we only support IPv4.  */
    colon = strrchr(s, ':');
    if (colon == NULL) {
        return -1;
    }

    host_len = (size_t) (colon - s);
    if (host_len == 0 || host_len >= host_bufsz) {
        return -1;
    }

    ngx_memcpy(host_buf, s, host_len);
    host_buf[host_len] = '\0';

    /* strtol() skips leading whitespace and accepts a sign, so "host: 80" and
     * "host:+80" both bound port 80 -- an address that does not look like the
     * one it becomes.  The port is digits, and nothing else. */
    if (colon[1] < '0' || colon[1] > '9') {
        return -1;
    }

    port = strtol(colon + 1, &endp, 10);
    if (*endp != '\0' || port < 1 || port > 65535) {
        return -1;
    }

    *port_out = (uint16_t) port;
    return 0;
}


/* ------------------------------------------------------------------ */
/* nginx.createSocket('host:port')                                      */
/* ------------------------------------------------------------------ */

static JSValue
ngx_js_create_socket(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    const char              *s;
    size_t                   slen;
    char                     host[48];
    char                     addr_str[64];
    uint16_t                 port;
    struct sockaddr_in       sin;
    int                      fd, opt, i, saved;
    uint32_t                 handle;
    ngx_js_socket_state_t   *st;
    ngx_js_worker_t         *w         = NULL;
    ngx_uint_t               local_slot = NGX_JS_LOCAL_SOCKET_REG_MAX;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx,
            "createSocket: expected string argument 'host:port'");
    }

    s = JS_ToCStringLen(ctx, &slen, argv[0]);
    if (!s) {
        return JS_EXCEPTION;
    }

    /*
     * An embedded NUL ends the C string early while the JS string carries on,
     * so "127.0.0.1:19112\0:19113" parsed as "127.0.0.1:19112" and bound a
     * port the caller never asked for -- the reviewed value and the bound
     * value were not the same value.  Refuse rather than silently truncate.
     */
    if (ngx_strlen(s) != slen) {
        JS_FreeCString(ctx, s);
        return JS_ThrowTypeError(ctx,
            "createSocket: address contains an embedded NUL");
    }

    if (ngx_js_parse_addr_port(s, host, sizeof(host), &port) != 0) {
        JS_FreeCString(ctx, s);
        return JS_ThrowTypeError(ctx,
            "createSocket: invalid address, expected 'host:port'");
    }

    JS_FreeCString(ctx, s);

    ngx_snprintf((u_char *) addr_str, sizeof(addr_str) - 1,
                 "%s:%d%Z", host, (int) port);

    /* Find a free registry slot. */
    handle = (uint32_t) NGX_JS_SOCKET_REG_MAX;
    for (i = 0; i < NGX_JS_SOCKET_REG_MAX; i++) {
        if (ngx_js_socket_reg[i] == NULL) {
            handle = (uint32_t) i;
            break;
        }
    }

    if (handle == (uint32_t) NGX_JS_SOCKET_REG_MAX) {
        return JS_ThrowInternalError(ctx,
            "createSocket: registry full (max %d sockets)",
            NGX_JS_SOCKET_REG_MAX);
    }

    if (ngx_process == NGX_PROCESS_WORKER) {
        /*
         * Phase F: post-fork worker — ask the master's manager thread to
         * create the socket and hand the fd back via SCM_RIGHTS.
         */
        fd = ngx_js_socket_mgr_create(addr_str, ngx_strlen(addr_str));
        if (fd < 0) {
            return JS_ThrowInternalError(ctx,
                "createSocket: manager failed to create socket for '%s'",
                addr_str);
        }

        /*
         * F3: verify there is a free slot in the worker-local registry
         * before allocating state, so we can always track this socket.
         */
        w = JS_GetContextOpaque(ctx);
        local_slot = NGX_JS_LOCAL_SOCKET_REG_MAX;
        if (w != NULL) {
            for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
                if (w->local_socket_reg[i] == NULL) {
                    local_slot = (ngx_uint_t) i;
                    break;
                }
            }
            if (local_slot == NGX_JS_LOCAL_SOCKET_REG_MAX) {
                close(fd);
                return JS_ThrowInternalError(ctx,
                    "createSocket: worker-local socket registry full"
                    " (max %d)", NGX_JS_LOCAL_SOCKET_REG_MAX);
            }
        }

    } else {
        /* Pre-fork (master / init_conf): create socket directly. */
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return JS_ThrowInternalError(ctx,
                "createSocket: socket() failed: %s", strerror(errno));
        }

        opt = 1;
        (void) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        ngx_memzero(&sin, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port   = htons(port);

        if (inet_pton(AF_INET, host, &sin.sin_addr) != 1) {
            close(fd);
            return JS_ThrowTypeError(ctx,
                "createSocket: invalid IPv4 address '%s'", host);
        }

        if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
            saved = errno;
            close(fd);
            return JS_ThrowInternalError(ctx,
                "createSocket: bind('%s') failed: %s",
                addr_str, strerror(saved));
        }

        if (listen(fd, 511) < 0) {
            saved = errno;
            close(fd);
            return JS_ThrowInternalError(ctx,
                "createSocket: listen() failed: %s", strerror(saved));
        }

        /*
         * The listening socket MUST be non-blocking.  nginx's event loop calls
         * accept() on a wakeup expecting EAGAIN when the backlog was already
         * drained by a peer worker (shared listener); on a blocking socket that
         * accept() sleeps in the kernel (wchan inet_csk_accept) and freezes the
         * whole worker.  nginx sets this in ngx_open_listening_sockets(), which
         * our injected fd bypasses, so we must do it ourselves.
         */
        if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) < 0) {
            saved = errno;
            close(fd);
            return JS_ThrowInternalError(ctx,
                "createSocket: set O_NONBLOCK failed: %s", strerror(saved));
        }
    }

    /* Allocate state.  Pre-fork: ngx_alloc() → COW-shared heap.
     * Post-fork: same allocator but memory is worker-private (no fork after). */
    st = ngx_alloc(sizeof(ngx_js_socket_state_t), ngx_cycle->log);
    if (st == NULL) {
        close(fd);
        return JS_ThrowInternalError(ctx, "createSocket: ngx_alloc failed");
    }

    st->fd          = fd;
    st->port        = port;
    st->in_listening = 0;
    st->owner        = ngx_js_current_compartment();   /* COMCON A1.1 */
    ngx_cpystrn((u_char *) st->addr, (u_char *) addr_str, sizeof(st->addr));

    /* New incarnation of this slot: every handle issued for the previous one
     * stops resolving here, which is what keeps a closed socket closed. */
    ngx_js_socket_reg_install(handle, st);

    /* F3: register in worker-local registry for cleanup tracking */
    if (ngx_process == NGX_PROCESS_WORKER && w != NULL
        && local_slot < NGX_JS_LOCAL_SOCKET_REG_MAX)
    {
        w->local_socket_reg[local_slot] = st;
    }

    return ngx_js_socket_wrap(ctx, handle);
}


uint32_t
ngx_js_socket_get_handle(JSValueConst sock)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(sock, ngx_js_socket_class_id);
    if (op == NULL) {
        return (uint32_t) NGX_JS_SOCKET_REG_MAX;
    }

    /*
     * Hand back the INDEX only while it still means the socket this object was
     * issued for.  Callers (attach) go straight from here to
     * ngx_js_socket_reg[handle], so returning a live index for a dead object
     * would launder a stale handle into a reference to the slot's new
     * occupant.
     */
    if (ngx_js_socket_state_of(op) == NULL) {
        return (uint32_t) NGX_JS_SOCKET_REG_MAX;
    }

    return op->handle;
}


ngx_int_t
ngx_js_socket_install(JSContext *ctx, JSValue nginx_obj)
{
    JS_SetPropertyStr(ctx, nginx_obj, "createSocket",
                      JS_NewCFunction(ctx, ngx_js_create_socket,
                                      "createSocket", 1));
    return NGX_OK;
}


/* ========================================================================= *
 * COMCON M-LIB `allowHosts` — the OUTBOUND capability
 * ========================================================================= *
 * See the block comment in ngx_js_socket.h for why this lives here and why it
 * records intent rather than performing I/O.
 */

typedef struct {
    char      method[8];
    char      url[NGX_JS_OUTBOUND_URL_LEN];
} ngx_js_outbound_rec_t;

typedef struct {
    ngx_uint_t             nrec;
    ngx_uint_t             dropped;   /* requests past the cap, counted not lost */
    ngx_js_outbound_rec_t  rec[NGX_JS_OUTBOUND_MAX_REC];
} ngx_js_outbound_state_t;

typedef struct {
    uint32_t  handle;
    uint32_t  gen;
    /*
     * The allowHosts glob.  glob_len == 0 means UNMEDIATED, which is only ever
     * the host's own wrapper: a granted wrapper is built by
     * ngx_js_outbound_wrap() and always carries one, because include() refuses a
     * grant whose flavour it cannot translate rather than defaulting to full
     * authority.
     */
    char      glob[NGX_JS_OUTBOUND_GLOB_LEN];
    size_t    glob_len;
    uint32_t  budget_limit;
    uint32_t  budget_window;
    char      budget_key[64];
    time_t    expires;
    uint32_t  win_days;
    uint32_t  win_from;
    uint32_t  win_to;
    /* M-LIB `cosign` -- see the socket opaque; quorum == 0 means uncosigned */
    uint32_t  cosign_quorum;
    uint32_t  cosign_within;
    char      cosign_key[64];
    char      cosign_as[48];
    /*
     * M-LIB `protocol`: a session type over this capability's operations.
     * proto_n == 0 means unsequenced.  Each term is (op id << 1) | starred, and
     * `proto_pos` is the cursor -- PER WRAPPER, deliberately not fleet-wide the
     * way a cosign record is: a session type describes ONE conversation, and two
     * holders sharing a cursor would interleave into nonsense.
     */
    uint8_t   proto_term[NGX_JS_PROTO_MAX];
    uint8_t   proto_n;
    uint8_t   proto_pos;
    /* see the socket opaque */
    uint32_t  owner;
} ngx_js_outbound_opaque_t;

JSClassID  ngx_js_outbound_class_id;   /* described by ngx_js_com_describe.c */

static ngx_js_outbound_state_t  *ngx_js_outbound_reg[NGX_JS_OUTBOUND_REG_MAX];
/*
 * A generation per slot, for the reason recorded in
 * bug-socket-handle-reuse-alias: an index into a reusable table is NOT a
 * capability.  A wrapper remembers the incarnation it was issued for, so a
 * stale wrapper cannot address whatever later took its slot.
 */
static uint32_t                  ngx_js_outbound_gen[NGX_JS_OUTBOUND_REG_MAX];


/*
 * A scheme-qualified host glob, spelled https + :// + a host glob.
 *
 * `protocol` in the vocabulary is NOT this -- MANUAL defines it as enforced
 * OPERATION ORDER ("handshake", "frames*", "close"), a session type over a
 * capability's methods, and that name is not free.  Restricting the scheme is an
 * attenuation of the DESTINATION, so it belongs inside allowHosts rather than in
 * a new vocabulary word invented outside the documented ten.
 *
 * A glob with no "://" matches any scheme, which is exactly today's behaviour --
 * so this narrows for whoever asks and changes nothing for whoever does not.  An
 * operator wanting TLS only writes the scheme; nothing is defaulted in the
 * permissive direction relative to what shipped.
 */
static ngx_int_t
ngx_js_outbound_glob_match(const u_char *glob, size_t glob_len,
    const u_char *scheme, size_t scheme_len, const u_char *host,
    size_t host_len)
{
    const u_char  *sep;
    size_t         gs_len;

    sep = (const u_char *) ngx_strlchr((u_char *) glob,
                                       (u_char *) glob + glob_len, ':');

    if (sep != NULL && (size_t) (glob + glob_len - sep) >= 3
        && sep[1] == '/' && sep[2] == '/')
    {
        gs_len = (size_t) (sep - glob);

        /* The scheme is matched EXACTLY, never globbed: "http*" would admit
         * both http and https, which is the opposite of what an operator
         * writing a scheme is asking for. */
        if (gs_len != scheme_len
            || ngx_strncasecmp((u_char *) glob, (u_char *) scheme, gs_len) != 0)
        {
            return 0;
        }

        glob = sep + 3;
        glob_len -= gs_len + 3;
    }

    return ngx_js_glob_match(glob, glob_len, host, host_len);
}


ngx_int_t
ngx_js_glob_match(const u_char *glob, size_t glob_len, const u_char *s,
    size_t s_len)
{
    size_t  p;

    if (glob_len == 1 && glob[0] == '*') {
        return 1;
    }

    /* leading star: "*.example.com" matches any host ending ".example.com".
     * Hosts wildcard on the left where paths wildcard on the right, which is
     * why one matcher has to know both -- two matchers would be two places for
     * the same rule to be wrong. */
    if (glob_len > 1 && glob[0] == '*') {
        p = glob_len - 1;
        return s_len >= p
               && ngx_strncmp(s + (s_len - p), glob + 1, p) == 0;
    }

    if (glob_len > 0 && glob[glob_len - 1] == '*') {
        p = glob_len - 1;
        return s_len >= p && ngx_strncmp(s, glob, p) == 0;
    }

    return s_len == glob_len && ngx_strncmp(s, glob, glob_len) == 0;
}


static void
ngx_js_outbound_finalizer(JSRuntime *rt, JSValue val)
{
    ngx_js_outbound_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_outbound_class_id);
    if (op) {
        js_free_rt(rt, op);
    }
}


static JSClassDef ngx_js_outbound_class = {
    "NginxOutbound",
    .finalizer = ngx_js_outbound_finalizer
};


int32_t
ngx_js_outbound_handle(JSValueConst val)
{
    ngx_js_outbound_opaque_t  *op;

    op = JS_GetOpaque(val, ngx_js_outbound_class_id);
    if (op == NULL) {
        return -1;
    }

    return (int32_t) op->handle;
}


static ngx_js_outbound_state_t *
ngx_js_outbound_state(ngx_js_outbound_opaque_t *op)
{
    if (op->handle >= NGX_JS_OUTBOUND_REG_MAX) {
        return NULL;
    }

    /* The generation check is the whole point of storing one. */
    if (ngx_js_outbound_gen[op->handle] != op->gen) {
        return NULL;
    }

    return ngx_js_outbound_reg[op->handle];
}


/*
 * The host part of the URL, without scheme, port, path or credentials.  Parsed
 * here rather than handed to a URL library because the glob is matched against
 * exactly this substring and nothing else: a matcher that sometimes sees
 * "example.com:8080" and sometimes "example.com" would make allowHosts mean two
 * different things depending on how the caller spelled a default port.
 */
static ngx_int_t
ngx_js_outbound_host(const char *url, size_t len, const char **scheme,
    size_t *scheme_len, const char **host, size_t *host_len)
{
    const char  *p, *end, *h;

    end = url + len;
    p = url;
    *scheme = url;
    *scheme_len = 0;

    /* scheme:// — required, so that "evil.com/?x=//good.com" cannot be read as
     * a host of "good.com" by a parser that merely searches for "//". */
    h = ngx_strlchr((u_char *) p, (u_char *) end, ':')
        ? (const char *) ngx_strlchr((u_char *) p, (u_char *) end, ':') : NULL;
    if (h == NULL || (size_t) (end - h) < 3 || h[1] != '/' || h[2] != '/') {
        return NGX_ERROR;
    }
    *scheme_len = (size_t) (h - url);
    p = h + 3;

    /* credentials are refused rather than skipped: "user@host" in an allowHosts
     * world is an invitation to smuggle a host past a glob. */
    for (h = p; h < end; h++) {
        if (*h == '@') {
            return NGX_ERROR;
        }
        if (*h == '/' || *h == '?' || *h == '#') {
            break;
        }
    }

    *host = p;
    *host_len = (size_t) (h - p);

    /* strip an explicit port */
    for (h = *host; h < *host + *host_len; h++) {
        if (*h == ':') {
            *host_len = (size_t) (h - *host);
            break;
        }
    }

    return (*host_len > 0) ? NGX_OK : NGX_ERROR;
}


/*
 * request(url[, method]) — record an outbound intent.
 *
 * GATE ORDER: lifetime, window, destination, cosignature, budget.  Every refusal
 * comes BEFORE the charge for the reason `ttl` established: spending budget on a
 * request that cannot happen makes the audit read as though the tenant were
 * still working.  A glob-refused destination is exactly as impossible as an
 * expired capability, so it is charged for exactly as little.
 *
 * `cosign` sits after the DESTINATION check here, one step later than in the
 * socket getter, and for the same reason the socket puts it after the window: a
 * signature must not be recorded for a request the mediation would refuse
 * outright.  Otherwise an operator could gather consent against
 * https://evil.example -- a destination this capability can never reach -- and
 * have it count toward the quorum for the destination it can.
 */
static JSValue
ngx_js_outbound_request(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_js_outbound_opaque_t  *op;
    ngx_js_outbound_state_t   *st;
    ngx_js_outbound_rec_t     *rec;
    const char                *url, *host, *meth, *scheme;
    size_t                     len, host_len, mlen, scheme_len;
    ngx_int_t                  pstep = NGX_ERROR;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_outbound_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    st = ngx_js_outbound_state(op);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx, "outbound: capability is closed");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "outbound.request: arg0 must be a URL");
    }

    url = JS_ToCStringLen(ctx, &len, argv[0]);
    if (url == NULL) {
        return JS_EXCEPTION;
    }

    if (ngx_js_cap_foreign(op->owner)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_OWNER, url))
    {
        JS_FreeCString(ctx, url);
        return JS_UNDEFINED;
    }

    if (op->expires != 0 && ngx_time() >= op->expires
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_EXPIRED, url))
    {
        JS_FreeCString(ctx, url);
        return JS_UNDEFINED;
    }

    if (ngx_js_window_closed(op->win_days, op->win_from, op->win_to)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_WINDOW, url))
    {
        JS_FreeCString(ctx, url);
        return JS_UNDEFINED;
    }

    if (ngx_js_outbound_host(url, len, &scheme, &scheme_len, &host, &host_len)
        != NGX_OK)
    {
        JS_FreeCString(ctx, url);
        return JS_ThrowTypeError(ctx,
            "outbound.request: arg0 must be an absolute scheme://host URL "
            "with no credentials");
    }

    /*
     * glob_len == 0 is the HOST's own wrapper, which is unmediated by
     * construction.  A granted wrapper always carries a glob, so this is not a
     * fall-through to full authority for a tenant -- include() refuses a grant
     * it cannot translate rather than defaulting.
     */
    if (op->glob_len > 0
        && !ngx_js_outbound_glob_match((u_char *) op->glob, op->glob_len,
                                       (u_char *) scheme, scheme_len,
                                       (u_char *) host, host_len))
    {
        if (ngx_js_compartment_denial(NGX_JS_DENIAL_OUT_HOST, url)) {
            JS_FreeCString(ctx, url);
            return JS_UNDEFINED;
        }
    }

    /* CHECKED here, COMMITTED after the budget -- see the socket getter for why
     * this gate is the one that has to split its decision from its effect. */
    if (op->proto_n > 0) {
        pstep = ngx_js_protocol_step(op->proto_term, op->proto_n,
                                     op->proto_pos, 0 /* request */);
        if (pstep == NGX_ERROR
            && ngx_js_compartment_denial(NGX_JS_DENIAL_CAP_PROTOCOL, url))
        {
            JS_FreeCString(ctx, url);
            return JS_UNDEFINED;
        }
    }

    if (op->cosign_quorum > 0
        && ngx_js_cosign_gate(ctx, op->cosign_key, op->cosign_as,
                              op->cosign_quorum, op->cosign_within, url)
           != NGX_OK)
    {
        JS_FreeCString(ctx, url);
        return JS_UNDEFINED;
    }

    if (op->budget_limit > 0
        && ngx_js_shared_budget_charge(ctx, op->budget_key, op->budget_limit,
                                       op->budget_window) != NGX_OK
        && ngx_js_compartment_denial(NGX_JS_DENIAL_BUDGET_USES, op->budget_key))
    {
        JS_FreeCString(ctx, url);
        return JS_UNDEFINED;
    }

    if (op->proto_n > 0 && pstep != NGX_ERROR) {
        op->proto_pos = (uint8_t) pstep;
    }

    /*
     * Past the cap the request is DROPPED AND COUNTED, never silently lost: a
     * queue that overflows quietly would let a fragment hide an intent behind
     * thirty-two others.
     */
    if (st->nrec >= NGX_JS_OUTBOUND_MAX_REC) {
        st->dropped++;
        JS_FreeCString(ctx, url);
        return JS_NewInt32(ctx, -1);
    }

    meth = "GET";
    mlen = 3;
    if (argc > 1 && JS_IsString(argv[1])) {
        const char *m = JS_ToCStringLen(ctx, &mlen, argv[1]);
        if (m != NULL) {
            rec = &st->rec[st->nrec];
            ngx_cpystrn((u_char *) rec->method, (u_char *) m,
                        sizeof(rec->method));
            JS_FreeCString(ctx, m);
            meth = NULL;
        }
    }

    rec = &st->rec[st->nrec];
    if (meth != NULL) {
        ngx_cpystrn((u_char *) rec->method, (u_char *) meth,
                    sizeof(rec->method));
    }
    ngx_cpystrn((u_char *) rec->url, (u_char *) url, sizeof(rec->url));
    st->nrec++;

    JS_FreeCString(ctx, url);
    return JS_NewInt32(ctx, (int32_t) st->nrec);
}


/*
 * pending() / clear() — the HOST's half.
 *
 * Denied inside a compartment by the A1 reach gate.  A fragment that could
 * drain the queue would read what a SIBLING fragment sharing the same cap had
 * recorded, which is a channel between tenants and not an outbound request --
 * the same reason a granted socket's `.listener` is denied even though the
 * fragment legitimately holds the socket.
 */
static JSValue
ngx_js_outbound_pending(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_js_outbound_opaque_t  *op;
    ngx_js_outbound_state_t   *st;
    JSValue                    out, arr, one;
    ngx_uint_t                 i;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_outbound_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (!ngx_js_compartment_may_reach(NGX_JS_COMPARTMENT_HOST_ROOT)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_OUT_DRAIN, "pending"))
    {
        return JS_UNDEFINED;
    }

    st = ngx_js_outbound_state(op);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx, "outbound: capability is closed");
    }

    out = JS_NewObject(ctx);
    arr = JS_NewArray(ctx);

    for (i = 0; i < st->nrec; i++) {
        one = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, one, "method",
                          JS_NewString(ctx, st->rec[i].method));
        JS_SetPropertyStr(ctx, one, "url", JS_NewString(ctx, st->rec[i].url));
        JS_SetPropertyUint32(ctx, arr, (uint32_t) i, one);
    }

    JS_SetPropertyStr(ctx, out, "requests", arr);
    JS_SetPropertyStr(ctx, out, "dropped",
                      JS_NewInt64(ctx, (int64_t) st->dropped));
    return out;
}


static JSValue
ngx_js_outbound_clear(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_js_outbound_opaque_t  *op;
    ngx_js_outbound_state_t   *st;

    op = JS_GetOpaque2(ctx, this_val, ngx_js_outbound_class_id);
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    if (!ngx_js_compartment_may_reach(NGX_JS_COMPARTMENT_HOST_ROOT)
        && ngx_js_compartment_denial(NGX_JS_DENIAL_OUT_DRAIN, "clear"))
    {
        return JS_UNDEFINED;
    }

    st = ngx_js_outbound_state(op);
    if (st == NULL) {
        return JS_ThrowInternalError(ctx, "outbound: capability is closed");
    }

    /*
     * clear(n) removes only the FIRST n records.  Without a count, a drain that
     * awaited I/O and then cleared would discard intents another request had
     * appended to the same capability meanwhile -- and std.outbound.perform()
     * documented that it did not do that, which was simply untrue until the
     * control for it failed to fire and the claim was checked.
     */
    if (argc > 0 && JS_IsNumber(argv[0])) {
        uint32_t  n = 0;
        ngx_uint_t k;

        JS_ToUint32(ctx, &n, argv[0]);
        if (n >= st->nrec) {
            st->nrec = 0;
        } else {
            for (k = 0; k + n < st->nrec; k++) {
                st->rec[k] = st->rec[k + n];
            }
            st->nrec -= n;
        }
        /* `dropped` counts overflow for the whole capability, so it is only
         * reset by a full clear -- a partial drain has not seen the overflow. */
        if (st->nrec == 0) {
            st->dropped = 0;
        }
        return JS_UNDEFINED;
    }

    st->nrec = 0;
    st->dropped = 0;
    return JS_UNDEFINED;
}


static const JSCFunctionListEntry ngx_js_outbound_proto_funcs[] = {
    JS_CFUNC_DEF("request", 2, ngx_js_outbound_request),
    JS_CFUNC_DEF("pending", 0, ngx_js_outbound_pending),
    JS_CFUNC_DEF("clear",   1, ngx_js_outbound_clear),
};


ngx_int_t
ngx_js_outbound_register_class(JSRuntime *rt)
{
    if (ngx_js_outbound_class_id == 0) {
        JS_NewClassID(&ngx_js_outbound_class_id);
    }

    return JS_NewClass(rt, ngx_js_outbound_class_id, &ngx_js_outbound_class) < 0
           ? NGX_ERROR : NGX_OK;
}


ngx_int_t
ngx_js_outbound_install_proto(JSContext *ctx)
{
    JSValue  proto;

    if (ngx_js_outbound_class_id == 0) {
        return NGX_OK;
    }

    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, ngx_js_outbound_proto_funcs,
                               (int) (sizeof(ngx_js_outbound_proto_funcs)
                                      / sizeof(ngx_js_outbound_proto_funcs[0])));
    JS_SetClassProto(ctx, ngx_js_outbound_class_id, proto);
    return NGX_OK;
}


static JSValue
ngx_js_outbound_new_obj(JSContext *ctx, uint32_t handle, const char *glob,
    size_t glob_len, const char *budget_key, uint32_t budget_limit,
    uint32_t budget_window, uint32_t ttl_seconds)
{
    JSValue                    obj;
    ngx_js_outbound_opaque_t  *op;

    op = js_mallocz(ctx, sizeof(ngx_js_outbound_opaque_t));
    if (op == NULL) {
        return JS_EXCEPTION;
    }

    op->handle = handle;
    op->gen = ngx_js_outbound_gen[handle];

    if (glob != NULL && glob_len > 0) {
        if (glob_len >= sizeof(op->glob)) {
            glob_len = sizeof(op->glob) - 1;
        }
        ngx_memcpy(op->glob, glob, glob_len);
        op->glob[glob_len] = '\0';
        op->glob_len = glob_len;
    }

    if (budget_key != NULL && budget_limit > 0) {
        ngx_cpystrn((u_char *) op->budget_key, (u_char *) budget_key,
                    sizeof(op->budget_key));
        op->budget_limit = budget_limit;
        op->budget_window = budget_window ? budget_window : 1;
    }

    if (ttl_seconds > 0) {
        op->expires = ngx_time() + (time_t) ttl_seconds;
    }

    obj = JS_NewObjectClass(ctx, ngx_js_outbound_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, op);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, op);
    return obj;
}


JSValue
ngx_js_outbound_wrap(JSContext *ctx, uint32_t handle, const char *glob,
    size_t glob_len, const char *budget_key, uint32_t budget_limit,
    uint32_t budget_window, uint32_t ttl_seconds)
{
    if (handle >= NGX_JS_OUTBOUND_REG_MAX
        || ngx_js_outbound_reg[handle] == NULL)
    {
        return JS_ThrowInternalError(ctx, "outbound: bad handle");
    }

    return ngx_js_outbound_new_obj(ctx, handle, glob, glob_len, budget_key,
                                   budget_limit, budget_window, ttl_seconds);
}


/* nginx.outbound() — the host mints one.  Unmediated here; a tenant only ever
 * sees the result of mediate(cap, allowHosts(glob)). */
static JSValue
ngx_js_create_outbound(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_js_outbound_state_t  *st;
    uint32_t                  h;

    for (h = 0; h < NGX_JS_OUTBOUND_REG_MAX; h++) {
        if (ngx_js_outbound_reg[h] == NULL) {
            break;
        }
    }

    if (h == NGX_JS_OUTBOUND_REG_MAX) {
        return JS_ThrowInternalError(ctx,
            "nginx.outbound: no free slot (max %d)", NGX_JS_OUTBOUND_REG_MAX);
    }

    st = ngx_alloc(sizeof(ngx_js_outbound_state_t), ngx_cycle->log);
    if (st == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    ngx_memzero(st, sizeof(ngx_js_outbound_state_t));

    ngx_js_outbound_reg[h] = st;
    ngx_js_outbound_gen[h]++;      /* a new incarnation of this slot */

    return ngx_js_outbound_new_obj(ctx, h, NULL, 0, NULL, 0, 0, 0);
}


ngx_int_t
ngx_js_outbound_install(JSContext *ctx, JSValue nginx_obj)
{
    JS_SetPropertyStr(ctx, nginx_obj, "outbound",
                      JS_NewCFunction(ctx, ngx_js_create_outbound,
                                      "outbound", 0));
    return NGX_OK;
}


/* ========================================================================= *
 * COMCON M-LIB `window` — a RECURRING capability lifetime (office hours)
 * ========================================================================= *
 * `ttl` says "for the next N seconds"; `window` says "on these days, between
 * these hours".  THREATS.md wants it for the signing key, where the useful
 * attenuation is not a countdown but a schedule.
 *
 * TIMES ARE UTC, and that is a decision rather than an oversight.  "Office
 * hours" is a local-time idea, but a gate whose behaviour depends on the host's
 * TZ setting is a gate that cannot be tested identically on two machines and
 * changes under a daylight-saving transition without anything being edited.  So
 * the operator converts, once, where they can see what they are doing, and the
 * spec carries what was meant.  The docs say so in the same words.
 *
 * The clock is ngx_time(), nginx's CACHED epoch -- the same clock `ttl` uses, so
 * the two cannot disagree about when "now" is, and nothing inside one handler
 * can cross a boundary mid-request.
 */

/* Minutes since Sunday 00:00 UTC, from nginx's cached clock. */
static ngx_uint_t
ngx_js_window_now(ngx_uint_t *wday)
{
    ngx_tm_t  tm;

    ngx_gmtime(ngx_time(), &tm);
    *wday = (ngx_uint_t) tm.ngx_tm_wday;          /* 0 = Sunday */
    return (ngx_uint_t) tm.ngx_tm_hour * 60 + (ngx_uint_t) tm.ngx_tm_min;
}


/*
 * Is the capability CLOSED right now?  days is a 7-bit mask (bit 0 = Sunday).
 *
 * from == to means "the whole day", not "no time at all": a window an operator
 * wrote as 00:00-00:00 is far more likely to mean "all day on these days" than
 * "never", and the direction that guesses must be the one that DENIES less only
 * when it is also the one the writing plainly meant.  A window that is never
 * open is spelled by granting nothing at all.
 *
 * from > to wraps past midnight (22:00-02:00), which is the shift pattern this
 * would otherwise be unable to express.
 */
ngx_int_t
ngx_js_window_closed(uint32_t days, uint32_t from, uint32_t to)
{
    ngx_uint_t  wday, now;

    if (days == 0) {
        return 0;                      /* no window configured */
    }

    now = ngx_js_window_now(&wday);

    if (!(days & (1u << wday))) {
        return 1;
    }

    if (from == to) {
        return 0;                      /* the whole of an allowed day */
    }

    if (from < to) {
        return !(now >= from && now < to);
    }

    /* wraps midnight: open from `from` to 24:00 and from 00:00 to `to` */
    return !(now >= from || now < to);
}


void
ngx_js_socket_set_window(JSValueConst obj, uint32_t days, uint32_t from,
    uint32_t to)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_socket_class_id);
    if (op == NULL || days == 0) {
        return;
    }

    op->win_days = days;
    op->win_from = from;
    op->win_to = to;
}


void
ngx_js_outbound_set_window(JSValueConst obj, uint32_t days, uint32_t from,
    uint32_t to)
{
    ngx_js_outbound_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_outbound_class_id);
    if (op == NULL || days == 0) {
        return;
    }

    op->win_days = days;
    op->win_from = from;
    op->win_to = to;
}


/*
 * Apply a cosignature requirement to an already-wrapped capability.
 *
 * quorum == 0, an empty key or an empty principal all leave the wrapper
 * UNCOSIGNED.  That looks like a fail-open and is not: nothing reaches here
 * except a policy descriptor that mediate() already validated, and mediate()
 * refuses a quorum below 2, an empty key and a missing principal with codes of
 * their own -- E_CAP_PRINCIPAL exists precisely so that "who is signing" is
 * settled at admission rather than defaulted here.  The guard is the same shape
 * as set_window's `days == 0`: this function does not invent policy.
 */
void
ngx_js_socket_set_cosign(JSValueConst obj, const char *key, const char *as,
    uint32_t quorum, uint32_t within)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_socket_class_id);
    if (op == NULL || quorum == 0 || key == NULL || *key == '\0'
        || as == NULL || *as == '\0')
    {
        return;
    }

    ngx_cpystrn((u_char *) op->cosign_key, (u_char *) key,
                sizeof(op->cosign_key));
    ngx_cpystrn((u_char *) op->cosign_as, (u_char *) as,
                sizeof(op->cosign_as));
    op->cosign_quorum = quorum;
    op->cosign_within = within ? within : 1;
}


/*
 * Apply a session type.  n == 0 leaves the wrapper unsequenced, which is every
 * capability nobody wrote a protocol for; the terms themselves were validated at
 * the producer, where the operator can be told which word they got wrong.
 */
/*
 * Bind a wrapper to the fragment it was granted to.  Applied after the wrapper
 * exists, like the window/cosign/protocol setters, and for the same reason: the
 * wrap signatures are long enough already and this is optional -- the host's own
 * wrappers are never bound.
 */
void
ngx_js_socket_set_owner(JSValueConst obj, uint32_t frag)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_socket_class_id);
    if (op != NULL) {
        op->owner = frag;
    }
}


void
ngx_js_outbound_set_owner(JSValueConst obj, uint32_t frag)
{
    ngx_js_outbound_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_outbound_class_id);
    if (op != NULL) {
        op->owner = frag;
    }
}


void
ngx_js_socket_set_protocol(JSValueConst obj, const uint8_t *term, ngx_uint_t n)
{
    ngx_js_socket_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_socket_class_id);
    if (op == NULL || n == 0 || n > NGX_JS_PROTO_MAX) {
        return;
    }

    ngx_memcpy(op->proto_term, term, n);
    op->proto_n = (uint8_t) n;
    op->proto_pos = 0;
}


void
ngx_js_outbound_set_protocol(JSValueConst obj, const uint8_t *term,
    ngx_uint_t n)
{
    ngx_js_outbound_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_outbound_class_id);
    if (op == NULL || n == 0 || n > NGX_JS_PROTO_MAX) {
        return;
    }

    ngx_memcpy(op->proto_term, term, n);
    op->proto_n = (uint8_t) n;
    op->proto_pos = 0;
}


void
ngx_js_outbound_set_cosign(JSValueConst obj, const char *key, const char *as,
    uint32_t quorum, uint32_t within)
{
    ngx_js_outbound_opaque_t  *op;

    op = JS_GetOpaque(obj, ngx_js_outbound_class_id);
    if (op == NULL || quorum == 0 || key == NULL || *key == '\0'
        || as == NULL || *as == '\0')
    {
        return;
    }

    ngx_cpystrn((u_char *) op->cosign_key, (u_char *) key,
                sizeof(op->cosign_key));
    ngx_cpystrn((u_char *) op->cosign_as, (u_char *) as,
                sizeof(op->cosign_as));
    op->cosign_quorum = quorum;
    op->cosign_within = within ? within : 1;
}
