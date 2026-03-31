# 11 — Key Data Structures

All structs are defined in `ngx_js.h` unless noted.  Field comments reflect
ownership and lifetime rules.

---

## ngx_js_conf_t  (cycle-level, lives in master heap)

```c
typedef struct {
    /* JS runtime — created in init_conf, inherited via COW by workers */
    JSRuntime          *rt;
    JSContext          *ctx;

    /* js_source directives */
    ngx_array_t         sources;        /* ngx_str_t[] of file paths */

    /* Back-pointer to the ngx_js_worker_t after fork */
    void               *worker;

    /* SharedWorker states (static, pre-fork) */
    ngx_js_sw_state_t  *sw_list;

    /* P11 cross-worker shared-memory zone */
    ngx_shm_zone_t     *shared_zone;

    /* nginx.on('message', fn) registry */
    JSValue             master_handlers;   /* plain JS object {event:[fn,...]} */
} ngx_js_conf_t;
```

One instance per nginx cycle.  Allocated in master's init_conf; workers get a
COW copy.  The `rt` and `ctx` pointers are the same address in all processes
(COW — not thread-safe to write without a write-trigger).

---

## ngx_js_worker_t  (per-worker, lives in worker heap)

```c
typedef struct {
    JSRuntime              *rt;
    JSContext              *ctx;

    /* Pending async operations — intrusive linked lists */
    ngx_js_async_ctx_t    *async_pending;   /* P1/P2 suspended handlers */
    ngx_js_bf_pending_t   *bf_pending;      /* P5 async whole-body filters */
    ngx_js_sf_pending_t   *sf_pending;      /* P5 async streaming filters */
    ngx_js_l4_pending_t   *l4_pending;      /* P12 async L4 generators */

    /* Request currently executing JS code (used for re-entry guard) */
    ngx_http_request_t    *current_request;

    /* Dynamic SharedWorkers created post-fork by this worker */
    ngx_js_sw_state_t     *local_sw_list;
} ngx_js_worker_t;
```

Allocated in `init_process`.  `rt`/`ctx` point to the same runtime as the master
(COW).  All pending lists are private to this worker.

---

## ngx_js_loc_conf_t  (per-location, lives in config pool)

```c
typedef struct {
    ngx_http_core_loc_conf_t  *core_clcf;

    /* P1 pre-content hooks */
    ngx_array_t                hook_fns;           /* JSValue[8] */

    /* P3 response-header hooks */
    ngx_array_t                response_hook_fns;  /* JSValue[8] */

    /* P5/P15 body filters */
    ngx_array_t                filter_fns;         /* JSValue[8] */

    /* P14 upstream filters */
    ngx_array_t                upstream_filter_fns;
    ngx_array_t                upstream_request_filter_fns;

    /* P7/P9 location content handler */
    JSValue                    handler;

    /* Snapshot bookkeeping for per-request mutations */
    unsigned                   snapshotted:1;
} ngx_js_loc_conf_t;
```

Lives in the nginx config memory pool.  The `JSValue` fields are rooted here;
they are freed in `exit_process` before `JS_FreeContext`.

---

## ngx_js_req_ctx_t  (per-request, lives in request pool)

```c
typedef struct {
    /* Filter engine state */
    ngx_chain_t    *body_bufs;           /* WB buffer accumulator */
    ngx_str_t       wb_body;             /* flattened whole-body string */
    ngx_chain_t    *stream_out;          /* streaming sendBuffer output */
    ngx_chain_t   **stream_out_last;
    ngx_uint_t      filter_idx;          /* current filter in the chain */
    JSValue         gen_obj;             /* active generator JS object */
    ngx_chain_t    *gen_out;             /* generator yield output */
    ngx_chain_t   **gen_out_last;

    /* req.ctx — persists across hook/filter invocations */
    JSValue         ctx_obj;             /* JS_UNDEFINED until first access */

    /* Response short-circuit flag */
    unsigned        responded:1;

    /* Mutation mode for the current request */
    uint32_t        write_mode;          /* NGX_JS_WRITE_GLOBAL / LOCAL */
    uint32_t        read_mode;

    /* Per-module config snapshots */
    ngx_js_module_snap_t  *snapped;
    unsigned               loc_conf_snapshotted:1;
    unsigned               core_clcf_snapshotted:1;
} ngx_js_req_ctx_t;
```

Allocated from the nginx request pool.  A pool cleanup callback frees `ctx_obj`
and `gen_obj` JSValues when the request is finalised.

---

## ngx_js_async_ctx_t  (one per suspended request)

```c
typedef struct ngx_js_async_ctx_s {
    ngx_http_request_t    *r;
    JSValue                req_obj;       /* DupValue — keeps wrapper alive */
    JSValue                promise;       /* outer chain Promise */
    ngx_js_async_ctx_t    *next;          /* link in w->async_pending */
    unsigned               is_hook:1;
    unsigned               is_p2_hook:1;
} ngx_js_async_ctx_t;
```

Lives in the request pool.  Linked into `w->async_pending` while the request
is suspended.  Removed by `ngx_js_async_check` when the Promise resolves.

---

## ngx_js_bf_pending_t  (suspended async body filter)

```c
typedef struct {
    JSValue                promise;        /* filter return Promise */
    JSValue                gen;            /* generator object (GENERATOR mode) */
    ngx_chain_t           *gen_out;
    ngx_chain_t          **gen_out_last;
    ngx_uint_t             resume_idx;     /* next filter index on resolve */
    ngx_js_worker_t       *w;
    ngx_http_request_t    *r;
    struct ngx_js_bf_pending_s *next;
} ngx_js_bf_pending_t;
```

Linked into `w->bf_pending`.  `ngx_js_bf_async_check` iterates this list and
calls `ngx_http_finalize_request` when the Promise resolves.

---

## ngx_js_sw_state_t  (per SharedWorker, lives in master heap)

```c
typedef struct {
    char                       *url;          /* script URL (heap) */
    size_t                      url_len;
    pthread_t                   tid;           /* SW pthread */
    int                         wake_pipe[2];  /* [0]=read (SW polls), [1]=write */
    ngx_js_channel_t           *channels;      /* per-worker socketpairs */
    ngx_uint_t                  nchannels;
    JSRuntime                  *rt;            /* SW's own JSRuntime */
    JSContext                  *ctx;
    ngx_js_sw_state_t          *next;
} ngx_js_sw_state_t;
```

`channels[wi]` is the socketpair for worker `wi`:
```c
typedef struct {
    int  sw_fd;       /* SW thread's end */
    int  worker_fd;   /* nginx worker's end */
} ngx_js_channel_t;
```

---

## ngx_js_sab_hdr_t  (embedded before every SAB data region)

```c
typedef struct {
    int      ref_count;   /* atomic; used for pre-fork (SHARED) SABs */
    uint32_t flags;       /* NGX_JS_SAB_SHARED = 1, NGX_JS_SAB_MEMFD = 2 */
    uint32_t size;        /* payload bytes */
    uint32_t _pad;
    uint64_t buf[0];      /* data region — pointer returned to JS */
} ngx_js_sab_hdr_t;
```

The `buf` member is at a 16-byte offset from the header start, ensuring natural
alignment for `int32_t` and `int64_t` TypedArray accesses.

---

## ngx_js_shared_entry_t  (P11 SHM zone entry)

```c
typedef struct {
    u_char  used;           /* 1 = live entry, 0 = unused slot */
    char    key[128];       /* null-terminated key */
    char    val[512];       /* null-terminated value */
} ngx_js_shared_entry_t;
```

256 entries × 641 bytes ≈ 163 KB zone.  Protected by a spinlock in
`ngx_js_shared_hdr_t`.

---

## ngx_js_plugin_t  (plugin registry entry)

```c
typedef struct {
    ngx_str_t  path;        /* resolved filesystem path */
    JSValue    config;      /* user-supplied config object */
    JSValue    instance;    /* return value from the plugin main fn */
} ngx_js_plugin_t;
```

Stored in `ngx_js_conf_t.plugins` (ngx_array_t).  Accessible as
`nginx.plugins[i].{path, config}` from JS.

---

## ngx_js_module_snap_t  (per-request config mutation)

```c
typedef struct {
    void          *original;   /* pointer to the shared module config */
    void          *copy;       /* per-request heap copy */
    ngx_module_t  *module;     /* identifies which module owns the struct */
    ngx_uint_t     level;      /* location / server / http level */
} ngx_js_module_snap_t;
```

Linked from `ngx_js_req_ctx_t.snapped`.  At request end, the `original` pointer
in nginx's location config is restored and the `copy` is returned to the request
pool.
