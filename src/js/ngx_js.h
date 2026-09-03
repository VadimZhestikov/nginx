
/*
 * Copyright (C) nginx JS contributors
 */

#ifndef _NGX_JS_H_INCLUDED_
#define _NGX_JS_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>


/* Forward declaration for SharedWorker state list */
struct ngx_js_sw_state_s;
typedef struct ngx_js_sw_state_s ngx_js_sw_state_t;

/*
 * Max payload bytes for a single nginx.sendToWorker() / nginx.sendToMaster()
 * message.  Serialized via JS_WriteObject; sent over the existing nginx
 * channel socketpair with a ngx_channel_t header (ch.fd = payload_len).
 */
#define NGX_JS_MSG_MAX  (64 * 1024)

/* Forward declaration to allow ngx_js_loc_conf_t to store original_handler */
struct ngx_http_request_s;
typedef ngx_int_t (*ngx_js_http_handler_pt)(struct ngx_http_request_s *r);

/* Forward declaration for socket state (ngx_js_socket.h) */
struct ngx_js_socket_state_s;
typedef struct ngx_js_socket_state_s ngx_js_socket_state_t;

#define NGX_JS_LOCAL_SOCKET_REG_MAX  32

/*
 * COMCON A2.1: a socket the host granted into the tenant compartment, by name.
 * The tenant eval re-wraps `handle` into the tenant context as `name`.
 */
typedef struct {
    ngx_str_t            name;
    uint32_t             handle;
} ngx_js_tenant_grant_t;


/*
 * COMCON B (E1): a pinned tenant dependency — a pure library evaluated in a
 * bare (no-capability) environment and bound on the tenant global as `name`,
 * admitted only if its bytes hash to `sha256`. A hijacked update (different
 * bytes) is refused at load; the dependency runs with what the consumer
 * granted (nothing), never what it requests.
 */
typedef struct {
    ngx_str_t            name;         /* NUL-terminated global bind name    */
    ngx_str_t            path;
    u_char               sha256[32];   /* expected content hash             */
} ngx_js_tenant_dep_t;


/*
 * COMCON C4: the fragment artifact ("fat bytecode"). The admitted fragment's
 * content-addressed identity plus its admission certificate, computed at
 * admission after the C3 checks pass. Identity = H(H(source) ∥ schema-version)
 * folds the content pin and the schema pin into one match (SPEC §8): it detects
 * both content drift (the source changed) and schema drift (the C2 surface the
 * fragment was admitted against changed). The env-signature is the free-name
 * manifest (C3.0); the certificate bits record which admission checks cleared.
 * No lowering here — the artifact is the T1-executable record C5 will lower.
 */
typedef struct {
    ngx_uint_t           built;             /* artifact computed this cycle    */
    u_char               content_hash[32];  /* SHA-256 over the tenant sources */
    u_char               identity[32];      /* SHA-256(content_hash‖schema-ver)*/
    ngx_uint_t           free_name_count;   /* env-signature size (C3.0)       */
    unsigned             cert_free_names:1; /* every free name resolved (C3.0) */
    unsigned             cert_no_dyn_code:1;/* no eval/Function/with (C3-rest) */
    unsigned             cert_request_sealed:1; /* no non-schema field (types) */
    unsigned             cert_onreq_sig:1;  /* onRequest (Request)=>Response    */
} ngx_js_artifact_t;

/* The C2 schema version this build admits against — the "schema hash" folded
 * into the artifact identity. Matches schema/tenant-env.schema.json `version`
 * (drift is caught by t/comcon_schema_conformance.t). */
#define NGX_JS_C4_SCHEMA_VERSION  "c2-tenant-env-1"

/* COMCON gas: default per-request execution budget (ms) for a confined tenant
 * handler. Enforced via the interrupt handler + request_deadline_ms around the
 * tenant JS_Call — bounds CPU time so an untrusted `while(true){}` cannot hang
 * the worker (memory is separately bounded by JS_SetMemoryLimit). A configurable
 * js_tenant_timeout directive is a follow-up; this is the safe default. */
#define NGX_JS_TENANT_TIMEOUT_MS  1000


/*
 * Per-cycle configuration owned by ngx_js_module (NGX_CORE_MODULE).
 * Allocated in cycle->pool via create_conf; populated by js_source
 * directives during ngx_conf_parse(), executed by init_conf().
 */
typedef struct {
    ngx_array_t          sources;         /* ngx_str_t: paths from js_source  */
    ngx_array_t          tenant_sources;  /* ngx_str_t: js_tenant_source paths
                                             — evaluated in a reduced,
                                             deny-by-default compartment (A2.0) */
    ngx_array_t          tenant_grants;   /* ngx_js_tenant_grant_t: sockets the
                                             host granted into the tenant (A2.1) */
    ngx_array_t          tenant_deps;     /* ngx_js_tenant_dep_t: pinned pure
                                             libraries (B/E1) */

    /*
     * COMCON A3: the persistent tenant compartment. One isolated runtime +
     * context for ALL js_tenant_source files (compartment 1), created in
     * init_conf before fork, COW-inherited by workers, serving requests via
     * js_tenant_handler locations. tenant_request_handler is the function the
     * tenant registered through its granted onRequest() — a GC-tracked JSValue
     * held in jcf, freed BEFORE JS_FreeContext (same rule as master_handlers).
     */
    JSRuntime           *tenant_rt;
    JSContext           *tenant_ctx;
    JSRuntime           *comcon_rt;       /* M-CFG: OWN runtime for the include
                                             compartment (mirrors tenant_rt — a
                                             dedicated runtime, so JS_FreeRuntime
                                             tears it down cleanly, unlike a 2nd
                                             context on the host runtime). */
    JSContext           *comcon_ctx;      /* confined compartment on comcon_rt */
    ngx_array_t         *comcon_frags;    /* JSValue[] confined fragments, held
                                             C-side; index = handle. Freed before
                                             JS_FreeContext(comcon_ctx). */
    JSValue              tenant_request_handler;
    ngx_uint_t           tenant_mode;     /* A4/B0: ngx_js_tenant_mode_e */

    ngx_js_artifact_t    tenant_artifact;      /* C4: the admitted-fragment record */
    u_char               tenant_artifact_pin[32]; /* C4: js_tenant_artifact pin    */
    ngx_uint_t           tenant_artifact_pinned;  /* C4: 1 if an identity is pinned */
    JSRuntime           *rt;              /* master-process QuickJS runtime   */
    JSContext           *ctx;             /* master-process QuickJS context   */
    void                *worker;          /* ngx_js_worker_t* after fork      */
    ngx_js_sw_state_t   *sw_list;        /* linked list of SharedWorker states */
    JSValue              master_handlers; /* {event:[fn,...]} — nginx.on() registry */
    ngx_shm_zone_t      *shared_zone;    /* P11: nginx.shared memory zone    */
} ngx_js_conf_t;


/*
 * P11 — nginx.shared: cross-worker shared key/value store.
 *
 * The zone is a flat array of fixed-size entries preceded by a header.
 * All cross-worker access is protected by an ngx_atomic spinlock.
 *
 * Layout inside the shared memory zone:
 *   [ngx_js_shared_hdr_t][ngx_js_shared_entry_t * capacity]
 */

#define NGX_JS_SHARED_KEY_LEN   128
#define NGX_JS_SHARED_VAL_LEN   512
#define NGX_JS_SHARED_CAPACITY  256
#define NGX_JS_SHARED_SIZE      \
    (sizeof(ngx_js_shared_hdr_t) \
     + NGX_JS_SHARED_CAPACITY * sizeof(ngx_js_shared_entry_t))

typedef struct {
    u_char  used;
    time_t  expires;   /* absolute expiry, ngx_time() seconds; 0 = never */
    char    key[NGX_JS_SHARED_KEY_LEN];
    char    val[NGX_JS_SHARED_VAL_LEN];
} ngx_js_shared_entry_t;

typedef struct {
    ngx_atomic_t  lock;
    ngx_uint_t    count;     /* number of used entries */
    ngx_uint_t    capacity;  /* total slots (= NGX_JS_SHARED_CAPACITY) */
    /* ngx_js_shared_entry_t entries[capacity] follow immediately */
} ngx_js_shared_hdr_t;


/*
 * Saved state for one suspended async nginx request.
 * Allocated in r->pool; freed automatically when the request pool is torn down.
 */
struct ngx_http_request_s;

typedef struct ngx_js_async_ctx_s  ngx_js_async_ctx_t;

struct ngx_js_async_ctx_s {
    struct ngx_http_request_s  *r;
    JSValue                     req_obj;   /* DupValue'd from content handler */
    JSValue                     promise;   /* outer handler Promise */
    ngx_js_async_ctx_t         *next;      /* intrusive list in async_pending */
    unsigned                    is_hook:1;    /* 1 = suspended in P1 hook chain */
    unsigned                    is_p2_hook:1; /* 1 = suspended in P2 access hook */
};


/* Forward declarations — full definitions follow ngx_js_worker_t below. */
typedef struct ngx_js_bf_pending_s  ngx_js_bf_pending_t;
typedef struct ngx_js_sf_pending_s  ngx_js_sf_pending_t;
typedef struct ngx_js_l4_pending_s  ngx_js_l4_pending_t;
typedef struct ngx_js_repl_conn_s   ngx_js_repl_conn_t;


/*
 * Per-worker JS runtime created in init_process().
 * Workers never share a JSRuntime — QuickJS is not thread-safe.
 */
typedef struct {
    JSRuntime               *rt;
    JSContext               *ctx;
    ngx_js_async_ctx_t      *async_pending;       /* list of suspended content handlers */
    ngx_js_bf_pending_t     *bf_pending;          /* list of suspended WB async filters */
    ngx_js_sf_pending_t     *sf_pending;          /* list of suspended streaming filters */
    ngx_js_l4_pending_t     *l4_pending;          /* P12: async L4 connections */
    ngx_js_repl_conn_t      *repl_pending;        /* active listen/listenRaw connections */
    ngx_js_sw_state_t       *local_sw_list;       /* dynamic SWs created post-fork  */
    uint64_t                 request_deadline_ms;  /* 0 = none; CLOCK_MONOTONIC ms   */
    size_t                   baseline_malloc_size; /* rt malloc_size right after fork */
    struct ngx_http_request_s *current_request;    /* non-NULL while JS runs in req  */
    ngx_array_t             *dispatching_hdr_arr;  /* set during header filter loop  */
    ngx_array_t             *dispatching_body_arr; /* set during body filter loop    */
    /*
     * F3 — Worker-local socket registry.
     * Parallel to ngx_js_socket_reg[] but scoped to this worker process.
     * Populated when createSocket() runs post-fork; used by exit_process
     * to close sockets that were never activated (in_listening == 0).
     */
    ngx_js_socket_state_t   *local_socket_reg[NGX_JS_LOCAL_SOCKET_REG_MAX];
    /*
     * F4 — broadcast socket fd (bcast_fds[ngx_worker][1]).
     * Stored at init_process; event is activated lazily on first request
     * to avoid calling ngx_get_connection before ngx_event_process_init.
     */
    int                      bcast_fd;
    ngx_connection_t        *bcast_conn;  /* non-NULL after activation */
} ngx_js_worker_t;


/*
 * Suspend/resume entry for a wholeBodyAsync filter.
 * Allocated in r->pool; linked into w->bf_pending.
 * For generator mode (NGX_JS_FILTER_GENERATOR):
 *   gen     — DupValue'd generator object (JS_UNDEFINED for non-generator)
 *   gen_out — accumulated output chain head from yielded values
 *   gen_out_last — tail pointer for gen_out
 */
struct ngx_js_bf_pending_s {
    JSValue                     promise;    /* DupValue'd filter return Promise */
    JSValue                     gen;        /* generator object or JS_UNDEFINED */
    ngx_chain_t                *gen_out;       /* output chain head for generator yields */
    ngx_chain_t               **gen_out_last;  /* tail pointer for gen_out */
    ngx_uint_t                  resume_idx; /* next filter index to run on resolve */
    ngx_js_worker_t            *w;
    struct ngx_http_request_s  *r;
    ngx_js_bf_pending_t        *next;
};


/*
 * Suspend/resume entry for a streamingAsync filter.
 * Allocated in r->pool; linked into w->sf_pending.
 * cur_data/cur_len is the chunk input to the async filter (pool-allocated).
 * Used as pass-through fallback if the Promise resolves to non-string.
 */
struct ngx_js_sf_pending_s {
    JSValue                     promise;
    ngx_uint_t                  resume_idx;
    ngx_js_worker_t            *w;
    struct ngx_http_request_s  *r;
    u_char                     *cur_data;
    size_t                      cur_len;
    ngx_uint_t                  is_last;
    ngx_js_sf_pending_t        *next;
};


/*
 * Write-mode flags for COM property setters.
 * NGX_JS_WRITE_GLOBAL — write to the shared (global) config struct only.
 * NGX_JS_WRITE_LOCAL  — write to the per-request snapshot only.
 * NGX_JS_WRITE_BOTH   — write to both (default assignment behaviour).
 */
#define NGX_JS_WRITE_GLOBAL  0x01u
#define NGX_JS_WRITE_LOCAL   0x02u
#define NGX_JS_WRITE_BOTH    (NGX_JS_WRITE_GLOBAL | NGX_JS_WRITE_LOCAL)


/*
 * Per-module snapshot entry — linked list of conf structs that have been
 * deep-copied into r->pool for the current request.
 */
typedef struct ngx_js_module_snap_s {
    ngx_uint_t                      ctx_index;  /* module->ctx_index */
    void                           *orig;       /* original global conf ptr */
    struct ngx_js_module_snap_s    *next;
} ngx_js_module_snap_t;


/*
 * Per-request JS context — allocated in r->pool, stored via ngx_http_set_ctx.
 * Tracks which conf structs have been snapshotted, and the active write/read
 * mode (set by r.location.setWriteMode / setReadMode).
 */
typedef struct {
    unsigned               loc_conf_snapshotted:1;  /* r->loc_conf array copied */
    unsigned               core_clcf_snapshotted:1; /* core loc_conf deep-copied */
    unsigned               p1_chain_done:1;         /* P1 hook chain completed   */
    uint32_t               write_mode;  /* NGX_JS_WRITE_GLOBAL by default */
    uint32_t               read_mode;   /* NGX_JS_WRITE_GLOBAL by default */
    ngx_js_module_snap_t  *snapped;     /* per-module snapshot linked list */
    ngx_chain_t           *body_bufs;      /* accumulated response body (E1) */
    ngx_chain_t          **body_bufs_last; /* tail pointer into body_bufs list */
    ngx_uint_t             active_filter_mode; /* NGX_JS_FILTER_* of running filter */
    ngx_str_t              wb_body;        /* whole-body filter: current body string */
    ngx_chain_t           *stream_out;     /* sendBuffer accumulator (streaming)    */
    ngx_chain_t          **stream_out_last;/* tail of stream_out                    */
    void                  *repl;           /* ngx_js_repl_conn_t* when hijacked     */
    JSValue                ctx_obj;        /* P10: req.ctx — persists across wrappers */
    unsigned               upstream_req_filtered:1; /* P14: request body transformed */
} ngx_js_req_ctx_t;


/*
 * One entry in a per-location header/body filter list.
 * fn_idx is an index into the global __ngx_filters__ JS array (keeps fn
 * GC-reachable).  name is empty (len==0) for unnamed filters.
 * priority controls auto-insertion order (lower = runs first, default 50).
 * mode is one of the NGX_JS_FILTER_* constants below (body filters only;
 * header filter entries always carry NGX_JS_FILTER_WB_SYNC).
 */
#define NGX_JS_FILTER_PRIORITY_DEFAULT  50

/* Body filter execution modes (first argument to addBodyFilter). */
#define NGX_JS_FILTER_WB_SYNC      0   /* 'wholeBodySync'   fn(req,body)→str     */
#define NGX_JS_FILTER_WB_ASYNC     1   /* 'wholeBodyAsync'  async fn(req,body)→str */
#define NGX_JS_FILTER_STREAM_SYNC  2   /* 'streamingSync'   fn(req,chunk,flags)  */
#define NGX_JS_FILTER_STREAM_ASYNC 3   /* 'streamingAsync'  async fn(req,chunk,flags) */
#define NGX_JS_FILTER_GENERATOR    4   /* async function*(body,req) yields chunks */

typedef struct {
    uint32_t    fn_idx;   /* index into global __ngx_filters__ array */
    ngx_str_t   name;
    ngx_int_t   priority;
    ngx_uint_t  mode;     /* NGX_JS_FILTER_* — body filters only     */
} ngx_js_filter_entry_t;


/* Shared limits for accept hook and L4 filter arrays (P4/P6/P17) */
#define NGX_JS_ACCEPT_HANDLERS_MAX   8
#define NGX_JS_L4_FILTERS_MAX        8


/*
 * Per-http{} (global) JS hook config owned by ngx_js_http_module.
 * hooks: array of uint32_t indices into __ngx_hooks__ JS array.
 */
typedef struct {
    ngx_array_t  *hooks;
} ngx_js_http_main_conf_t;


/*
 * Per-server{} JS hook config owned by ngx_js_http_module.
 * hooks: array of uint32_t indices into __ngx_hooks__ JS array.
 *
 * P17: accept_handlers / l4_filters / l4_send_filters are parallel to
 * the arrays in ngx_js_http_listener_state_t and are filled by
 * server.on('accept', fn) / server.addL4Filter(fn) / server.addL4SendFilter(fn)
 * during the init_conf phase.  srv_listener_state is a pointer to the
 * ngx_js_http_listener_state_t that is built from these arrays after
 * all JS scripts have been evaluated; it is used by the accept handler
 * that overrides ls->handler on standard listen sockets.
 */
typedef struct {
    ngx_array_t  *hooks;
    /* P17: standard-socket accept hooks and L4 filters */
    uint32_t      accept_handlers[NGX_JS_ACCEPT_HANDLERS_MAX];
    ngx_uint_t    n_accept_handlers;
    uint32_t      l4_filters[NGX_JS_L4_FILTERS_MAX];
    ngx_uint_t    n_l4_filters;
    uint32_t      l4_send_filters[NGX_JS_L4_FILTERS_MAX];
    ngx_uint_t    n_l4_send_filters;
    void         *srv_listener_state;  /* ngx_js_http_listener_state_t* */
} ngx_js_http_srv_conf_t;


/*
 * Per-location JS handler config owned by ngx_js_http_module.
 * handler_idx == -1 means no JS handler is set for this location.
 * Otherwise it is an index into the global __ngx_handlers__ array
 * that was populated by location.handler = <function> assignments
 * during the config phase.
 *
 * header_filters / body_filters: NULL means no filters for this location.
 * own_header_filters / own_body_filters: 1 = array belongs to this conf,
 * 0 = pointer inherited from parent (copy-on-first-write on next mutation).
 */
typedef struct {
    ngx_int_t             handler_idx;
    ngx_js_http_handler_pt original_handler; /* clcf->handler before first JS
                                              * assignment; restored by
                                              * location.clearHandler()       */
    ngx_array_t          *header_filters;    /* ngx_js_filter_entry_t[]      */
    ngx_array_t          *body_filters;      /* ngx_js_filter_entry_t[]      */
    ngx_uint_t            own_header_filters;/* 1 = owned; 0 = inherited     */
    ngx_uint_t            own_body_filters;
    ngx_uint_t            body_filter_has_wb;/* 1 if any WB_SYNC/WB_ASYNC/GENERATOR */
    ngx_pool_t           *pool; /* cf->pool from create_loc_conf             */
    ngx_array_t          *hooks;           /* array of uint32_t fn indices into __ngx_hooks__ */
    ngx_uint_t            own_hooks;       /* 1 = owned; 0 = inherited ptr */
    ngx_array_t          *response_hooks;    /* uint32_t[] fn indices into __ngx_hooks__ */
    ngx_uint_t            own_response_hooks; /* 1 = owned; 0 = inherited */

    /* P14: upstream JS filters */
    ngx_array_t          *upstream_filters;     /* ngx_js_filter_entry_t[] — response */
    ngx_uint_t            own_upstream_filters;
    ngx_array_t          *upstream_req_filters; /* ngx_js_filter_entry_t[] — request  */
    ngx_uint_t            own_upstream_req_filters;
} ngx_js_loc_conf_t;


extern ngx_module_t  ngx_js_module;
extern ngx_module_t  ngx_js_http_module;

/* COMCON M-CFG (scope isolation, dedicated-runtime model): compile a fragment
 * in the confined compartment (own runtime) and hold it there (return an int
 * handle); invoke by handle with JSON data marshaled across the boundary (C
 * strings only — works even across runtimes). Defined in ngx_js_module.c,
 * registered on `comcon` in ngx_js_com.c. */
JSValue ngx_js_comcon_include_confined(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
JSValue ngx_js_comcon_invoke_confined(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
JSValue ngx_js_comcon_op_mode(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);
JSValue ngx_js_comcon_op_tenant(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);


/* COM initialisation — installs nginx.* into ctx's global object */
ngx_int_t  ngx_js_com_init(JSContext *ctx, ngx_cycle_t *cycle);

/*
 * Phase 1/2 — remove all listening socket read events from this worker's
 * event loop.  Equivalent to nginx's internal ngx_disable_accept_events
 * (cycle, 1) but implemented locally since that function is file-static.
 * Returns NGX_OK or NGX_ERROR.
 */
ngx_int_t  ngx_js_disable_accept_events(ngx_cycle_t *cycle);

/*
 * F4 — lazily activate the per-worker bcast event handler.
 * Safe to call multiple times (no-op if already activated).
 * Must only be called from within the worker's nginx event loop
 * (i.e., after ngx_event_process_init has run).
 */
void  ngx_js_bcast_ensure_active(ngx_js_worker_t *w);

/*
 * config.write(text) — feeds config text back into ngx_conf_parse() via a
 * temporary file.  ctx's opaque must be a valid ngx_conf_t*.  Usable from
 * both js_preprocess (core-level) and js_init_http (http-level) handlers.
 */
JSValue    ngx_js_config_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv);

/* Read a file into a NUL-terminated cycle->pool buffer. */
u_char    *ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path,
    size_t *len);

/* Log a pending JS exception to the NGINX error log, then clear it */
void       ngx_js_log_exception(JSContext *ctx, ngx_log_t *log);

/*
 * P16: load a plugin from `dir` (absolute path) into ctx/rt.
 * config_json — JSON string used as nginx.pluginConfig (NULL → "{}").
 * Returns NGX_OK on success, NGX_ERROR on failure.
 */
ngx_int_t  ngx_js_load_plugin(JSContext *ctx, JSRuntime *rt,
    ngx_cycle_t *cycle, const char *dir, const char *config_json);

/*
 * P16: send an nginx channel message (header + payload) over fd.
 * Used by both ngx_js_com.c and ngx_js_module.c for channel communication.
 */
ngx_int_t  ngx_js_channel_send(ngx_socket_t fd, ngx_uint_t command,
    ngx_uint_t slot_arg, const uint8_t *buf, size_t len, ngx_log_t *log);

/*
 * Compile and execute a JS file as an ES module.  Handles the compile-only
 * → JS_EvalFunction → drain-jobs → promise-rejection-check sequence that
 * QuickJS requires for module-mode evaluation.
 * Returns NGX_CONF_OK or NGX_CONF_ERROR (exception already logged).
 */
char      *ngx_js_eval_module(JSContext *ctx, JSRuntime *rt,
    const u_char *src, size_t src_len, const u_char *filename,
    ngx_log_t *log);

/* Register NginxRequest class in rt (called once per new runtime) */
ngx_int_t  ngx_js_request_register_class(JSRuntime *rt);

/* Install shared NginxRequest prototype in ctx (called once per new context) */
ngx_int_t  ngx_js_request_install_proto(JSContext *ctx);

/* Wrap an ngx_http_request_t as a JS NginxRequest object */
JSValue  ngx_js_wrap_request(JSContext *ctx, struct ngx_http_request_s *r);

/*
 * Run all JS header/body filters registered for jlcf's location.
 * Filters are called synchronously; exceptions are logged and skipped.
 * For body filters, body is the flattened response body (whole-body mode).
 * Returns NGX_OK or NGX_ERROR.
 */
ngx_int_t  ngx_js_header_filters_run(JSContext *ctx, JSRuntime *rt,
    struct ngx_http_request_s *r, ngx_js_loc_conf_t *jlcf);

/*
 * Run all JS body filters registered for jlcf's location (whole-body mode).
 * body     — the flat response body to transform.
 * out_body — receives the transformed body (allocated in r->pool).
 * If all filters return undefined/null, out_body == *body unchanged.
 * start_idx — first filter index to run (0 for initial call, i+1 on resume).
 * Returns NGX_OK (all done), NGX_AGAIN (async suspension, w->bf_pending set),
 * or NGX_ERROR.
 */
ngx_int_t  ngx_js_body_filters_run(JSContext *ctx, JSRuntime *rt,
    struct ngx_http_request_s *r, ngx_js_loc_conf_t *jlcf,
    ngx_str_t *body, ngx_str_t *out_body, ngx_uint_t start_idx);

/*
 * Run the whole-body filter chain from start_idx onwards.
 * rctx->wb_body holds the current body on entry and the final body on exit.
 * Emits the result downstream via ngx_js_next_body_filter on completion.
 * Returns NGX_OK or NGX_ERROR.
 * (Extended in later steps to handle async and streaming modes.)
 */
ngx_int_t  ngx_js_body_filter_run_from(ngx_js_worker_t *w,
    struct ngx_http_request_s *r, ngx_js_req_ctx_t *rctx,
    ngx_js_loc_conf_t *jlcf, ngx_uint_t start_idx);

/*
 * Run all streamingSync/streamingAsync filters in jlcf for one chunk.
 * chunk_data/chunk_len is the current output chunk; is_last=1 if last_buf.
 * start_idx: first filter to run (0 = initial call, i+1 = after async resume).
 * Filters emit output by calling req.sendBuffer(); STREAM_ASYNC filters
 * return a Promise whose resolved string becomes the next cur_data.
 * Returns NGX_OK, NGX_AGAIN (async suspension, w->sf_pending set), or NGX_ERROR.
 */
ngx_int_t  ngx_js_streaming_filters_run(JSContext *ctx, JSRuntime *rt,
    struct ngx_http_request_s *r, ngx_js_loc_conf_t *jlcf,
    u_char *chunk_data, size_t chunk_len, ngx_uint_t is_last,
    ngx_uint_t start_idx);

/*
 * Run the streaming filter chain from start_idx.  Resets rctx->stream_out,
 * calls ngx_js_streaming_filters_run, then emits the accumulated output
 * downstream (or an empty last_buf marker if dropped).
 * Returns NGX_OK, NGX_AGAIN (async, count already incremented), or NGX_ERROR.
 */
ngx_int_t  ngx_js_streaming_run_from(ngx_js_worker_t *w,
    struct ngx_http_request_s *r, ngx_js_req_ctx_t *rctx,
    ngx_js_loc_conf_t *jlcf, u_char *cur_data, size_t cur_len,
    ngx_uint_t is_last, ngx_uint_t start_idx);

/* Install shared NginxPendingServer prototype in ctx */
ngx_int_t  ngx_js_pending_server_install_proto(JSContext *ctx);

/* Content-phase handler; installed in clcf->handler by the JS setter */
struct ngx_http_request_s;
ngx_int_t  ngx_js_content_handler(struct ngx_http_request_s *r);

/*
 * Ensure a per-request snapshot of r->loc_conf exists.
 * On first call: allocates a private copy of the loc_conf pointer array in
 * r->pool and marks the request as snapshotted so subsequent calls are
 * no-ops.  Individual setters deep-copy their own module conf struct after
 * calling this.
 * Returns NGX_OK on success or NGX_ERROR on allocation failure.
 * Must only be called while w->current_request == r.
 */
ngx_int_t  ngx_js_ensure_snapshot(struct ngx_http_request_s *r);

/*
 * Ensure the ngx_http_core_loc_conf_t for r has been deep-copied into r->pool.
 * Calls ngx_js_ensure_snapshot first (idempotent), then copies the core
 * loc_conf struct so individual fields can be modified per-request without
 * affecting the shared config.  Subsequent calls are no-ops.
 */
ngx_int_t  ngx_js_ensure_core_snapshot(struct ngx_http_request_s *r);

/*
 * Deep-copy a module's loc_conf struct into r->pool and update
 * r->loc_conf[module->ctx_index].  Idempotent (no-op on repeat calls
 * for the same module).  Calls ngx_js_ensure_snapshot internally.
 * Returns NGX_OK or NGX_ERROR on alloc failure.
 */
ngx_int_t  ngx_js_ensure_module_snapshot(struct ngx_http_request_s *r,
    ngx_module_t *module, size_t conf_size);

/*
 * Returns 1 if op_conf is the current request's own conf for this module:
 * either r->loc_conf[ctx_index] == op_conf (not yet snapshotted), or
 * the snap list records that the current snapshot was made from op_conf.
 * Returns 0 for cross-location access.
 */
int  ngx_js_is_own_conf(struct ngx_http_request_s *r,
    ngx_js_req_ctx_t *rctx, ngx_uint_t ctx_index, void *op_conf);

/*
 * Inspect the promise of a suspended async request and finalize it if
 * the promise has settled.  Called from the timer handler in ngx_js_com.c.
 */
void ngx_js_async_check(ngx_js_worker_t *w);

/*
 * Drain all pending async HTTP requests with a 503 response.
 * Called from ngx_js_exit_process() before SharedWorker sockets are torn
 * down, so that a graceful shutdown does not deadlock when a SW thread
 * dies without replying.
 */
void ngx_js_async_drain_503(ngx_js_worker_t *w);

/*
 * Inspect pending wholeBodyAsync filter promises and resume or finalize
 * each settled entry.  Called alongside ngx_js_async_check from every
 * event-loop post-drain site.
 */
void ngx_js_bf_async_check(ngx_js_worker_t *w);

/*
 * Inspect pending streamingAsync filter promises and resume or finalize
 * each settled entry.  Called alongside ngx_js_async_check from every
 * event-loop post-drain site.
 */
void ngx_js_sf_async_check(ngx_js_worker_t *w);

/*
 * Inspect pending L4 async generator connections and finalize settled ones.
 * Called alongside ngx_js_async_check from every event-loop post-drain site.
 */
void ngx_js_l4_async_check(ngx_js_worker_t *w);

/*
 * Close all pending L4 connections on worker exit, freeing JSValues and
 * calling ngx_close_connection() to prevent the "open socket" ALERT from
 * ngx_worker_process_exit().  Must be called before JS_FreeContext().
 */
void ngx_js_l4_drain_exit(ngx_js_worker_t *w);
void ngx_js_repl_drain_exit(ngx_js_worker_t *w);

/*
 * P14: Run upstream response/request filters on body in-place.
 * Both are GENERATOR-mode only, synchronous (no await).
 * body is updated in-place with the filtered result.
 */
ngx_int_t  ngx_js_upstream_filters_run(JSContext *ctx, JSRuntime *rt,
    struct ngx_http_request_s *r, ngx_array_t *filters, ngx_str_t *body);
ngx_int_t  ngx_js_upstream_req_filters_run(JSContext *ctx, JSRuntime *rt,
    struct ngx_http_request_s *r, ngx_array_t *filters, ngx_str_t *body);


/*
 * ------------------------------------------------------------------ *
 * Shared-memory SharedArrayBuffer infrastructure                       *
 * ------------------------------------------------------------------ *
 *
 * All SABs created by any JS runtime in this module use
 * mmap(MAP_SHARED|MAP_ANONYMOUS).  Because the mapping is established
 * before fork(), the same physical pages are visible at the same
 * virtual address in the master process AND in every nginx worker
 * process, making cross-process pointer passing correct.
 *
 * SABs created in a worker process after fork() have NGX_JS_SAB_SHARED
 * clear; the SharedWorker pipe layer rejects them with a TypeError so
 * the user gets a clear error instead of a SIGSEGV.
 *
 * Header layout (total 16 bytes; buf[] is at offset 16, 8-byte aligned):
 *
 *   offset  0  int      ref_count   (atomic)
 *   offset  4  uint32_t flags
 *   offset  8  uint32_t size        payload bytes
 *   offset 12  uint32_t _pad
 *   offset 16  uint64_t buf[]       SAB data (pointer passed to QuickJS)
 */

#define NGX_JS_SAB_SHARED  0x01u   /* mmap'd before fork — valid in all processes */
#define NGX_JS_SAB_MEMFD   0x02u   /* memfd-backed — fd tracked in per-process table */

typedef struct {
    int      ref_count;
    uint32_t flags;
    uint32_t size;
    uint32_t _pad;
    uint64_t buf[0];
} ngx_js_sab_hdr_t;


void  ngx_js_sab_dup(void *opaque, void *ptr);
void  ngx_js_sab_free(void *opaque, void *ptr);
void *ngx_js_sab_alloc(void *opaque, size_t size);

/*
 * Return the memfd fd for a worker-created SAB, or -1 if not memfd.
 * Used by the channel layer to pass the fd via SCM_RIGHTS.
 */
int   ngx_js_sab_get_fd(void *ptr);

/*
 * Register a memfd SAB mapping received from another process.
 * ptr  — data pointer (buf[] address in THIS process's mapping)
 * fd   — the local memfd fd (dup'd from SCM_RIGHTS)
 * size — payload bytes (== ngx_js_sab_hdr_t.size)
 * The mapping starts with local_refs=1; the caller releases this ref
 * after JS_ReadObject has taken its own ref via sab_dup.
 */
void  ngx_js_sab_register_memfd(void *ptr, int fd, size_t size);

extern const JSSharedArrayBufferFunctions  ngx_js_sab_funcs;

/*
 * Set to 1 inside an SW thread so ngx_js_sab_alloc uses memfd even
 * when ngx_process == NGX_PROCESS_MASTER.
 */
extern __thread int  ngx_js_sw_thread_active;


#endif /* _NGX_JS_H_INCLUDED_ */
