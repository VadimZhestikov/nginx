
/*
 * Copyright (C) nginx JS contributors
 *
 * ngx_js_module — NGX_CORE_MODULE that embeds QuickJS into NGINX.
 *
 * Lifecycle:
 *   create_conf  — allocate ngx_js_conf_t in cycle->pool
 *   [ngx_conf_parse runs; js_source directives populate jcf->sources;
 *    js_preprocess directives run immediately and may inject config text]
 *   init_conf    — create JSRuntime/JSContext, install COM, eval scripts
 *   init_process — each worker inherits jcf->rt/ctx directly (fork COW);
 *                  no new runtime, no re-evaluation, no I/O
 *   exit_process — free worker's private copy of the runtime
 *   exit_master  — free master's original runtime
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <quickjs-libc.h>
#include "ngx_js.h"
#include "ngx_js_com.h"
#include "ngx_js_compartment.h"
#include "ngx_js_socket.h"

#include <openssl/sha.h>
#include "ngx_js_sw.h"
#include "vendor/acorn_js.h"
#include "ngx_js_listener.h"


/* ------------------------------------------------------------------ */
/* Shared-memory SAB allocator (see ngx_js.h for design rationale)    */
/* ------------------------------------------------------------------ */

/*
 * Per-process table of memfd-backed SABs.
 *
 * Workers create SABs post-fork using memfd_create so the backing memory
 * can be passed to the master (SharedWorker thread) via SCM_RIGHTS.
 * Each process tracks its own {data-pointer → memfd fd} mapping.
 * local_refs counts JS references within THIS process only.
 */

#ifdef SYS_memfd_create
# ifndef MFD_CLOEXEC
#  define MFD_CLOEXEC  1U
# endif
static int
ngx_memfd_create(const char *name, unsigned int flags)
{
    return (int) syscall(SYS_memfd_create, name, flags);
}
# define NGX_JS_HAVE_MEMFD  1
#endif


#define NGX_JS_SAB_FD_TABLE_MAX  64

typedef struct {
    void     *ptr;        /* SAB data pointer (buf[]) in this process */
    int       fd;         /* memfd fd; -1 = slot unused */
    size_t    size;       /* payload bytes */
    uint32_t  local_refs; /* ref count within this process */
} ngx_js_sab_fd_entry_t;

static ngx_js_sab_fd_entry_t  ngx_js_sab_fd_table[NGX_JS_SAB_FD_TABLE_MAX];
static pthread_mutex_t         ngx_js_sab_fd_lock = PTHREAD_MUTEX_INITIALIZER;
static int                     ngx_js_sab_fd_initialized;


static void
ngx_js_atfork_prepare(void)
{
    pthread_mutex_lock(&ngx_js_sab_fd_lock);
}


static void
ngx_js_atfork_parent(void)
{
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);
}


static void
ngx_js_atfork_child(void)
{
    /* Re-init: child inherited a locked mutex but no thread holds it */
    pthread_mutex_init(&ngx_js_sab_fd_lock, NULL);
}


static void
ngx_js_sab_fd_table_init(void)
{
    int  i;

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        ngx_js_sab_fd_table[i].fd = -1;
    }

    ngx_js_sab_fd_initialized = 1;
}


/* Find entry index by ptr; caller must hold the lock. */
static int
ngx_js_sab_fd_find(void *ptr)
{
    int  i;

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        if (ngx_js_sab_fd_table[i].fd >= 0
            && ngx_js_sab_fd_table[i].ptr == ptr)
        {
            return i;
        }
    }

    return -1;
}


void *
ngx_js_sab_alloc(void *opaque, size_t size)
{
    ngx_js_sab_hdr_t  *hdr;
    size_t             total;
#ifdef NGX_JS_HAVE_MEMFD
    int                fd, i;
#endif

    total = sizeof(ngx_js_sab_hdr_t) + size;

    if ((ngx_process == NGX_PROCESS_MASTER
         || ngx_process == NGX_PROCESS_SINGLE)
        && !ngx_js_sw_thread_active)
    {
        /* Pre-fork master main thread: MAP_SHARED|MAP_ANONYMOUS — same VA
         * in all workers.  SW threads (post-fork, master process) fall
         * through to memfd so the fd can be passed via SCM_RIGHTS. */
        hdr = mmap(NULL, total, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (hdr == MAP_FAILED) {
            return NULL;
        }

        hdr->ref_count = 1;
        hdr->flags     = NGX_JS_SAB_SHARED;
        hdr->size      = (uint32_t) size;
        hdr->_pad      = 0;

        return (void *) hdr->buf;
    }

#ifdef NGX_JS_HAVE_MEMFD
    /* Worker (post-fork): use memfd so the fd can be passed via SCM_RIGHTS */
    fd = ngx_memfd_create("ngx_js_sab", MFD_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }

    if (ftruncate(fd, (off_t) total) < 0) {
        close(fd);
        return NULL;
    }

    hdr = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (hdr == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    hdr->ref_count = 0;   /* not used for memfd SABs; tracked in fd table */
    hdr->flags     = NGX_JS_SAB_MEMFD;
    hdr->size      = (uint32_t) size;
    hdr->_pad      = 0;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        if (ngx_js_sab_fd_table[i].fd < 0) {
            ngx_js_sab_fd_table[i].ptr        = (void *) hdr->buf;
            ngx_js_sab_fd_table[i].fd         = fd;
            ngx_js_sab_fd_table[i].size        = size;
            ngx_js_sab_fd_table[i].local_refs  = 1;
            pthread_mutex_unlock(&ngx_js_sab_fd_lock);
            return (void *) hdr->buf;
        }
    }

    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    /* Table full */
    munmap(hdr, total);
    close(fd);
    return NULL;

#else
    /* No memfd support: fall back to MAP_SHARED|MAP_ANONYMOUS (no cross-process) */
    hdr = mmap(NULL, total, PROT_READ | PROT_WRITE,
               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (hdr == MAP_FAILED) {
        return NULL;
    }

    hdr->ref_count = 1;
    hdr->flags     = 0;
    hdr->size      = (uint32_t) size;
    hdr->_pad      = 0;

    return (void *) hdr->buf;
#endif
}


void
ngx_js_sab_dup(void *opaque, void *ptr)
{
    ngx_js_sab_hdr_t  *hdr;
    int                idx;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);
    idx = ngx_js_sab_fd_find(ptr);
    if (idx >= 0) {
        ngx_js_sab_fd_table[idx].local_refs++;
        pthread_mutex_unlock(&ngx_js_sab_fd_lock);
        return;
    }
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    /* Pre-fork SAB: use shared atomic ref count */
    hdr = (ngx_js_sab_hdr_t *) ptr - 1;
    __atomic_fetch_add(&hdr->ref_count, 1, __ATOMIC_SEQ_CST);
}


void
ngx_js_sab_free(void *opaque, void *ptr)
{
    ngx_js_sab_hdr_t  *hdr;
    size_t             total;
    int                idx, fd;
    size_t             size;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);
    idx = ngx_js_sab_fd_find(ptr);
    if (idx >= 0) {
        ngx_js_sab_fd_table[idx].local_refs--;
        if (ngx_js_sab_fd_table[idx].local_refs > 0) {
            pthread_mutex_unlock(&ngx_js_sab_fd_lock);
            return;
        }

        fd   = ngx_js_sab_fd_table[idx].fd;
        size = ngx_js_sab_fd_table[idx].size;
        ngx_js_sab_fd_table[idx].fd  = -1;
        ngx_js_sab_fd_table[idx].ptr = NULL;
        pthread_mutex_unlock(&ngx_js_sab_fd_lock);

        hdr   = (ngx_js_sab_hdr_t *) ptr - 1;
        total = sizeof(ngx_js_sab_hdr_t) + size;
        munmap(hdr, total);
        close(fd);
        return;
    }
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    /* Pre-fork SAB: free when shared ref count reaches zero */
    hdr = (ngx_js_sab_hdr_t *) ptr - 1;
    if (__atomic_fetch_add(&hdr->ref_count, -1, __ATOMIC_SEQ_CST) != 1) {
        return;
    }

    total = sizeof(ngx_js_sab_hdr_t) + hdr->size;
    munmap(hdr, total);
}


int
ngx_js_sab_get_fd(void *ptr)
{
    int  idx, fd;

    if (!ngx_js_sab_fd_initialized) {
        return -1;
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);
    idx = ngx_js_sab_fd_find(ptr);
    fd  = (idx >= 0) ? ngx_js_sab_fd_table[idx].fd : -1;
    pthread_mutex_unlock(&ngx_js_sab_fd_lock);

    return fd;
}


void
ngx_js_sab_register_memfd(void *ptr, int fd, size_t size)
{
    int  i;

    if (!ngx_js_sab_fd_initialized) {
        ngx_js_sab_fd_table_init();
    }

    pthread_mutex_lock(&ngx_js_sab_fd_lock);

    for (i = 0; i < NGX_JS_SAB_FD_TABLE_MAX; i++) {
        if (ngx_js_sab_fd_table[i].fd < 0) {
            ngx_js_sab_fd_table[i].ptr        = ptr;
            ngx_js_sab_fd_table[i].fd         = fd;
            ngx_js_sab_fd_table[i].size        = size;
            ngx_js_sab_fd_table[i].local_refs  = 1;
            break;
        }
    }

    pthread_mutex_unlock(&ngx_js_sab_fd_lock);
}


const JSSharedArrayBufferFunctions  ngx_js_sab_funcs = {
    ngx_js_sab_alloc,
    ngx_js_sab_free,
    ngx_js_sab_dup,
    NULL,
};


static void *ngx_js_create_conf(ngx_cycle_t *cycle);
static char *ngx_js_init_conf(ngx_cycle_t *cycle, void *conf);

/* M-CFG: the active jcf, cached at init_conf so comcon.include()/invoke() reach
 * it without ngx_cycle (which is not yet the current cycle during init_conf —
 * ngx_cycle->conf_ctx would be stale/NULL). COW-inherited by workers. */
static ngx_js_conf_t  *ngx_js_comcon_jcf;

static int       ngx_js_interrupt_handler(JSRuntime *rt, void *opaque);
static ngx_int_t ngx_js_init_module(ngx_cycle_t *cycle);
static ngx_int_t ngx_js_init_process(ngx_cycle_t *cycle);
static void      ngx_js_exit_process(ngx_cycle_t *cycle);
static void      ngx_js_exit_master(ngx_cycle_t *cycle);
static void      ngx_js_bcast_recv_handler(ngx_event_t *ev);
static void      ngx_js_handle_worker_load_plugin(ngx_socket_t fd,
    ngx_int_t payload_len);
static void      ngx_js_handle_worker_channel_msg(ngx_socket_t fd,
    ngx_int_t payload_len);
static void      ngx_js_handle_master_channel_msgs(ngx_cycle_t *cycle);
static void      ngx_js_dispatch_master_event(ngx_cycle_t *cycle,
    const char *event, ngx_pid_t pid, ngx_int_t slot, int status);

static char   *ngx_js_source(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static JSContext *ngx_js_tenant_context_new(JSRuntime *trt);
static ngx_int_t ngx_js_tenant_lockdown(JSContext *tctx, JSRuntime *trt,
    ngx_log_t *log);
static char   *ngx_js_preprocess(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_js_commands[] = {

    /*
     * js_source /path/to/script.js;
     *
     * Loads and evaluates a JavaScript file once the full nginx.conf
     * has been parsed (in init_conf).  Relative paths are resolved
     * against the directory of nginx.conf.  Multiple directives are
     * executed in declaration order.
     */
    { ngx_string("js_source"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_js_source,
      0,
      0,
      NULL },

    /*
     * js_preprocess /path/to/script.js;
     *
     * JS config preprocessor — the script is evaluated immediately
     * when this directive is encountered during ngx_conf_parse().
     * The script receives a global `config` object with a single
     * method: config.write(text) feeds `text` back into the nginx
     * config parser as if it had appeared inline.  This lets JS
     * generate any nginx directives (including full http{}/server{}
     * blocks) from files, environment variables, or external sources.
     *
     * The JS runtime used here is short-lived and independent of the
     * runtime created by js_source/init_conf.  nginx.http.servers[]
     * and other post-parse COM objects are NOT available.
     *
     * Multiple js_preprocess directives are allowed; each runs in its
     * own isolated runtime.
     */
    { ngx_string("js_preprocess"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_js_preprocess,
      0,
      0,
      NULL },

    ngx_null_command
};


static ngx_core_module_t  ngx_js_module_ctx = {
    ngx_string("js"),
    ngx_js_create_conf,
    ngx_js_init_conf
};


ngx_module_t  ngx_js_module = {
    NGX_MODULE_V1,
    &ngx_js_module_ctx,                /* module context */
    ngx_js_commands,                   /* module directives */
    NGX_CORE_MODULE,                   /* module type */
    NULL,                              /* init master */
    ngx_js_init_module,                /* init module */
    ngx_js_init_process,               /* init process */
    NULL,                              /* init thread */
    NULL,                              /* exit thread */
    ngx_js_exit_process,               /* exit process */
    ngx_js_exit_master,                /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_js_create_conf(ngx_cycle_t *cycle)
{
    ngx_js_conf_t  *jcf;

    jcf = ngx_pcalloc(cycle->pool, sizeof(ngx_js_conf_t));
    if (jcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&jcf->sources, cycle->pool, 4,
                       sizeof(ngx_str_t)) != NGX_OK)
    {
        return NULL;
    }

    if (ngx_array_init(&jcf->tenant_sources, cycle->pool, 2,
                       sizeof(ngx_str_t)) != NGX_OK)
    {
        return NULL;
    }

    if (ngx_array_init(&jcf->tenant_grants, cycle->pool, 2,
                       sizeof(ngx_js_tenant_grant_t)) != NGX_OK)
    {
        return NULL;
    }

    if (ngx_array_init(&jcf->tenant_deps, cycle->pool, 2,
                       sizeof(ngx_js_tenant_dep_t)) != NGX_OK)
    {
        return NULL;
    }

    /* rt, ctx, worker, sw_list, tenant_rt, tenant_ctx are 0/NULL after pcalloc */
    jcf->master_handlers = JS_UNINITIALIZED;
    jcf->tenant_request_handler = JS_UNINITIALIZED;

    return jcf;
}


static char *
ngx_js_source(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_js_conf_t  *jcf = conf;
    ngx_str_t      *value, *path;

    value = cf->args->elts;    /* value[0] = "js_source", value[1] = path */

    path = ngx_array_push(&jcf->sources);
    if (path == NULL) {
        return NGX_CONF_ERROR;
    }

    *path = value[1];

    /* Resolve relative path against the directory of nginx.conf */
    if (ngx_conf_full_name(cf->cycle, path, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}








/*
 * COMCON A3: tear down the persistent tenant compartment of this process's
 * copy of jcf. The GC-tracked handler JSValue must be freed BEFORE
 * JS_FreeContext (the "list_empty(&rt->gc_obj_list)" rule, as with
 * master_handlers). Safe to call when no tenant exists; idempotent.
 */
static void
ngx_js_tenant_teardown(ngx_js_conf_t *jcf)
{
    if (jcf->tenant_ctx != NULL) {
        if (!JS_IsUninitialized(jcf->tenant_request_handler)) {
            JS_FreeValue(jcf->tenant_ctx, jcf->tenant_request_handler);
            jcf->tenant_request_handler = JS_UNINITIALIZED;
        }
        JS_FreeContext(jcf->tenant_ctx);
        jcf->tenant_ctx = NULL;
    }

    if (jcf->tenant_rt != NULL) {
        JS_FreeRuntime(jcf->tenant_rt);
        jcf->tenant_rt = NULL;
    }
}




/* ------------------------------------------------------------------ */
/* COMCON B0: the learn-mode recorder — a catch-all object that records  */
/* every access path a tenant walks into the withheld host surface.      */
/* ------------------------------------------------------------------ */

static JSClassID  ngx_js_recorder_class_id;

static JSValue ngx_js_recorder_new(JSContext *ctx, const char *path);


static void
ngx_js_recorder_finalizer(JSRuntime *rt, JSValue val)
{
    char  *path;

    path = JS_GetOpaque(val, ngx_js_recorder_class_id);
    if (path != NULL) {
        js_free_rt(rt, path);
    }
}


static JSValue
ngx_js_recorder_get(JSContext *ctx, JSValueConst obj, JSAtom atom,
    JSValueConst receiver)
{
    char        *path;
    const char  *key;
    u_char       child[128], *e;

    key = JS_AtomToCString(ctx, atom);
    if (key == NULL) {
        return JS_UNDEFINED;
    }

    /* only identifier-like names — skip symbols, indices, coercion probes */
    if (!((key[0] >= 'a' && key[0] <= 'z') || (key[0] >= 'A' && key[0] <= 'Z')
          || key[0] == '_' || key[0] == '$'))
    {
        JS_FreeCString(ctx, key);
        return JS_UNDEFINED;
    }

    path = JS_GetOpaque(obj, ngx_js_recorder_class_id);

    e = ngx_snprintf(child, sizeof(child) - 1, "%s.%s",
                     path ? path : "?", key);
    *e = '\0';

    ngx_js_learn_record((const char *) child);
    JS_FreeCString(ctx, key);

    return ngx_js_recorder_new(ctx, (const char *) child);
}


static JSValue
ngx_js_recorder_get_proto(JSContext *ctx, JSValueConst obj)
{
    return JS_NULL;
}


static int
ngx_js_recorder_set_proto(JSContext *ctx, JSValueConst obj,
    JSValueConst proto)
{
    return 1;                             /* ignore */
}


static int
ngx_js_recorder_set(JSContext *ctx, JSValueConst obj, JSAtom atom,
    JSValueConst value, JSValueConst receiver, int flags)
{
    return 1;                             /* swallow writes */
}


static int
ngx_js_recorder_has(JSContext *ctx, JSValueConst obj, JSAtom atom)
{
    return 1;
}


static JSValue
ngx_js_recorder_call(JSContext *ctx, JSValueConst func_obj,
    JSValueConst this_val, int argc, JSValueConst *argv, int flags)
{
    char    *path;
    u_char   callp[128], *e;

    path = JS_GetOpaque(func_obj, ngx_js_recorder_class_id);

    e = ngx_snprintf(callp, sizeof(callp) - 1, "%s()", path ? path : "?");
    *e = '\0';

    ngx_js_learn_record((const char *) callp);

    return ngx_js_recorder_new(ctx, path ? path : "?");
}


static JSClassExoticMethods  ngx_js_recorder_exotic = {
    .get_property  = ngx_js_recorder_get,
    .set_property  = ngx_js_recorder_set,
    .has_property  = ngx_js_recorder_has,
    .get_prototype = ngx_js_recorder_get_proto,
    .set_prototype = ngx_js_recorder_set_proto,
};

static JSClassDef  ngx_js_recorder_class = {
    "ComconRecorder",
    .finalizer = ngx_js_recorder_finalizer,
    .call      = ngx_js_recorder_call,
    .exotic    = &ngx_js_recorder_exotic,
};


static JSValue
ngx_js_recorder_new(JSContext *ctx, const char *path)
{
    JSValue  obj;
    size_t   len;
    char    *copy;

    /* JS_NULL proto: required for consistent exotic get_property behaviour. */
    obj = JS_NewObjectProtoClass(ctx, JS_NULL, ngx_js_recorder_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    len = ngx_strlen(path);
    if (len > 120) {
        len = 120;
    }

    copy = js_malloc(ctx, len + 1);
    if (copy != NULL) {
        ngx_memcpy(copy, path, len);
        copy[len] = '\0';
        JS_SetOpaque(obj, copy);
    }

    JS_SetConstructorBit(ctx, obj, 1);    /* so `new Worker()` records too */

    return obj;
}


/* Seed globalThis with a recorder for each withheld host-authority name that
 * is not already granted — learn mode only. */
static void
ngx_js_learn_seed(JSContext *ctx, JSValue global)
{
    static const char *const withheld[] = {
        "nginx", "createSocket", "fetch", "Worker", "SharedWorker",
        "config", "use", "install", "require", "broadcast", "std", "os",
        NULL
    };

    JSValue     existing;
    ngx_uint_t  i;
    ngx_flag_t  present;

    for (i = 0; withheld[i] != NULL; i++) {
        existing = JS_GetPropertyStr(ctx, global, withheld[i]);
        present = !JS_IsUndefined(existing);
        JS_FreeValue(ctx, existing);

        if (!present) {
            JS_SetPropertyStr(ctx, global, withheld[i],
                              ngx_js_recorder_new(ctx, withheld[i]));
        }
    }
}


/*
 * COMCON A2/A3: the tenant compartment. ONE isolated runtime + context
 * (compartment 1) shared by all js_tenant_source files — deny-by-default:
 * the global has only granted names (report, onRequest, the host's grants) —
 * no `nginx`, no module loader (free imports fail), no dangerous
 * constructors. This is the primary confinement control; the reach-registry
 * gates (A1) are the defense-in-depth behind it.
 *
 * A3: the runtime PERSISTS in jcf (COW-inherited by workers) so that the
 * handler registered via onRequest() can serve requests; torn down wherever
 * the host runtime is (failed init_conf, reload old-cycle, exit_process,
 * exit_master).
 */


/*
 * COMCON M-SES-0: build the tenant context with a CURATED intrinsic set.
 * Everything JS_NewContext installs EXCEPT Proxy (membrane-defeating; THREATS
 * LOW-6 — nothing in the tenant path needs it). The Eval intrinsic MUST stay:
 * it installs the compiler entry point (ctx->eval_internal) that every JS_Eval
 * — including MODULE compilation, i.e. how tenant sources run — depends on;
 * omitting it disables running any tenant code. The reflective `eval` GLOBAL it
 * also installs, and the dynamic-code reach welded into JS_AddIntrinsicBaseObjects
 * (the Function constructor, hence `.constructor.constructor`; and Reflect), are
 * removed afterwards by ngx_js_tenant_lockdown().
 */
static JSContext *
ngx_js_tenant_context_new(JSRuntime *trt)
{
    JSContext  *tctx;

    tctx = JS_NewContextRaw(trt);
    if (tctx == NULL) {
        return NULL;
    }

    if (JS_AddIntrinsicBaseObjects(tctx)
        || JS_AddIntrinsicDate(tctx)
        || JS_AddIntrinsicEval(tctx)
        || JS_AddIntrinsicStringNormalize(tctx)
        || JS_AddIntrinsicRegExp(tctx)
        || JS_AddIntrinsicJSON(tctx)
        || JS_AddIntrinsicMapSet(tctx)
        || JS_AddIntrinsicTypedArrays(tctx)
        || JS_AddIntrinsicPromise(tctx)
        || JS_AddIntrinsicWeakRef(tctx))
    {
        JS_FreeContext(tctx);
        return NULL;
    }

    return tctx;
}


/*
 * COMCON M-SES-0: SES-style lockdown. The Function constructor (and the
 * generator / async / async-generator function constructors, reached via their
 * prototypes' `constructor`) and Reflect remain after the curated intrinsics
 * because BaseObjects welds them in. Neutralize them so DYNAMIC CODE is truly
 * unreachable — this is what makes C3-rest's "no dynamic code" guarantee sound
 * and the C4 free-name manifest a complete over-approximation (front-end audit
 * finding A1). Runs in the tenant context BEFORE any tenant code (deps or
 * sources). The taming stubs are frozen (non-writable, non-configurable) so a
 * tenant cannot restore them. It also deletes the reflective `eval` global
 * (the Eval intrinsic had to stay for the compiler; deleting the global removes
 * the JS-reachable `eval`) — and `Proxy` was omitted at context creation.
 */
static const char  ngx_js_tenant_lockdown_js[] =
    "(function () {"
    "  function block() {"
    "    throw new TypeError('dynamic code disabled (COMCON M-SES)');"
    "  }"
    "  function tame(proto) {"
    "    if (proto) {"
    "      Object.defineProperty(proto, 'constructor', {"
    "        value: block, writable: false, enumerable: false,"
    "        configurable: false"
    "      });"
    "    }"
    "  }"
    "  tame(Function.prototype);"
    "  tame(Object.getPrototypeOf(function* () {}));"
    "  tame(Object.getPrototypeOf(async function () {}));"
    "  tame(Object.getPrototypeOf(async function* () {}));"
    "  delete globalThis.Function;"
    "  delete globalThis.Reflect;"
    "  delete globalThis.eval;"
    "})();"
    /* COMCON M-SES-1: transitively FREEZE the intrinsic graph so a tenant can
     * neither pollute a shared prototype (Object.prototype.x = ...) nor tamper
     * with a built-in — mutations that otherwise PERSIST across requests in the
     * long-lived tenant runtime. Runs before the caps (report/onRequest/grants)
     * and the COM prototypes are installed, and never freezes globalThis itself
     * (so those can still be added afterwards). Standard JS still works: only
     * MUTATING intrinsics is refused; creating/using instances is unaffected. */
    "(function () {"
    "  var seen = new Set();"
    "  function harden(o) {"
    "    if (o === null || o === globalThis) return;"
    "    var t = typeof o;"
    "    if (t !== 'object' && t !== 'function') return;"
    "    if (seen.has(o)) return;"
    "    seen.add(o);"
    "    Object.freeze(o);"
    "    var n = Object.getOwnPropertyNames(o), i, d;"
    "    for (i = 0; i < n.length; i++) {"
    "      d = Object.getOwnPropertyDescriptor(o, n[i]); if (!d) continue;"
    "      if ('value' in d) harden(d.value);"
    "      if (d.get) harden(d.get); if (d.set) harden(d.set);"
    "    }"
    "    var s = Object.getOwnPropertySymbols(o), j, e;"
    "    for (j = 0; j < s.length; j++) {"
    "      e = Object.getOwnPropertyDescriptor(o, s[j]); if (!e) continue;"
    "      if ('value' in e) harden(e.value);"
    "      if (e.get) harden(e.get); if (e.set) harden(e.set);"
    "    }"
    "    harden(Object.getPrototypeOf(o));"
    "  }"
    "  var r = Object.getOwnPropertyNames(globalThis), k, rd;"
    "  for (k = 0; k < r.length; k++) {"
    "    rd = Object.getOwnPropertyDescriptor(globalThis, r[k]);"
    "    if (rd && ('value' in rd)) harden(rd.value);"
    "  }"
    "  harden(Object.getPrototypeOf(function* () {}));"
    "  harden(Object.getPrototypeOf(async function () {}));"
    "  harden(Object.getPrototypeOf(async function* () {}));"
    "  harden(Object.getPrototypeOf([][Symbol.iterator]()));"
    /* SR-3: shared iterator instance-prototypes the value-walk never reaches (they
       exist only as the result of calling a method, so no property path leads to
       them). Without these a tenant can mutate the shared %StringIteratorProto% /
       %Map|SetIteratorProto% / %RegExpStringIteratorProto% and pollute the next
       request. (%Generator|AsyncGeneratorPrototype% are already frozen via the
       %Generator|AsyncGenerator%.prototype value edge above; a generator function's
       own .prototype is per-function and isolated, so it is left alone.) */
    "  harden(Object.getPrototypeOf(''[Symbol.iterator]()));"
    "  harden(Object.getPrototypeOf(new Map()[Symbol.iterator]()));"
    "  harden(Object.getPrototypeOf(new Set()[Symbol.iterator]()));"
    "  try { harden(Object.getPrototypeOf(''.matchAll(/(?:)/g))); } catch (e) {}"
    "})();";

static ngx_int_t
ngx_js_tenant_lockdown(JSContext *tctx, JSRuntime *trt, ngx_log_t *log)
{
    char  *rc;

    /* Run as a MODULE, not JS_EVAL_TYPE_GLOBAL — indirect (global) eval needs
     * the Eval intrinsic we deliberately omitted, whereas module compilation
     * (the same path tenant sources take) does not. */
    rc = ngx_js_eval_module(tctx, trt,
                            (const u_char *) ngx_js_tenant_lockdown_js,
                            sizeof(ngx_js_tenant_lockdown_js) - 1,
                            (const u_char *) "<comcon-m-ses-lockdown>", log);

    return (rc == NGX_CONF_OK) ? NGX_OK : NGX_ERROR;
}


/*
 * COMCON M-SES-1b: freeze the capability prototypes a granted fragment/tenant
 * can reach. ngx_js_com_install_protos() runs AFTER ngx_js_tenant_lockdown's
 * M-SES-1 freeze and installs the COM/Socket class protos via JS_SetClassProto
 * (off globalThis, so the M-SES-1 value-walk never reaches them) — they are
 * unfrozen. A default confined profile cannot reach them (no grant), but a LIVE
 * grant hands over a cap whose prototype is then reachable
 * (Object.getPrototypeOf(granted)); without this, one fragment/tenant could
 * pollute a shared cap prototype and poison another's cap. Grants today are
 * sockets only (grantToTenant / include both reject non-sockets), so the
 * reachable surface is the socket family (socket + its listener reach edge).
 * MUST grow with any new grantable cap kind (COM nodes). NEVER call this for the
 * regular pilgrim host context — it legitimately mutates COM protos for dynamic
 * reconfig.
 *
 * The hardening is done via the C API — NOT `Object.freeze` in the compartment,
 * which empirically corrupts the compartment's subsequent parser (a QuickJS
 * interaction). Each cap proto is made NON-EXTENSIBLE (JS_PreventExtensions):
 * a fragment cannot PLANT a new property on the shared cap prototype — the
 * primary cross-fragment pollution vector (a property that persists and is seen
 * by another fragment's cap). This is deliberately the extensibility subset:
 * also locking the EXISTING getters non-configurable (to block SHADOWING them
 * via redefinition) requires either the in-compartment Object.freeze (corrupts
 * the parser) or a C-side JS_DefineProperty redefine (destabilizes the socket
 * state — breaks the reach gate). Both are engine-level interactions; closing
 * the shadowing residual is deferred to an engine-level getter-hardening pass.
 * MUST grow with any new grantable cap kind. NEVER call this for the regular
 * pilgrim host context — it legitimately mutates COM protos for reconfig.
 */
static void
ngx_js_comcon_harden_cap_protos(JSContext *ctx)
{
    JSValue     proto;
    ngx_uint_t  i;
    JSClassID   ids[] = { ngx_js_socket_class_id,
                          ngx_js_http_listener_class_id,
                          ngx_js_stream_listener_class_id,
                          ngx_js_com_facet_class_id };

    for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        proto = JS_GetClassProto(ctx, ids[i]);
        if (JS_IsObject(proto)) {
            (void) JS_PreventExtensions(ctx, proto);
        }
        JS_FreeValue(ctx, proto);
    }
}


/*
 * A FRAGMENT'S CONTINUATION CAN FAIL, and until this existed nothing said so.
 *
 * The obvious place to catch it was the return value of JS_ExecutePendingJob --
 * and that is wrong, which is worth recording because it looked right and the
 * test written against it passed for the wrong reason.  A promise reaction job
 * that throws does not FAIL: the promise machinery catches the throw and rejects
 * the derived promise, so the job returns success and the failure becomes an
 * UNHANDLED REJECTION.  `.then(function(){ throw x; })` takes that path, which
 * is the shape a deferred continuation actually has.
 *
 * So the report comes from the rejection tracker instead.  Nothing in this
 * process installed one, on either runtime, so an unhandled rejection was
 * silent everywhere; this covers the compartment, where async fragments made
 * continuations reachable in the first place.
 *
 * ONE LINE PER INVOCATION plus a count, the same discipline the denial log uses:
 * a fragment that queues ten thousand rejecting jobs must not be able to turn
 * its own bug into a log flood.
 */
static ngx_uint_t  ngx_js_comcon_rejections;
static ngx_uint_t  ngx_js_comcon_rejections_logged;


static void
ngx_js_comcon_rejection_tracker(JSContext *ctx, JSValueConst promise,
    JSValueConst reason, JS_BOOL is_handled, void *opaque)
{
    const char  *s;

    if (is_handled) {
        return;                 /* somebody caught it; not our business */
    }

    ngx_js_comcon_rejections++;

    if (ngx_js_comcon_rejections_logged > 0) {
        return;
    }

    ngx_js_comcon_rejections_logged = 1;

    s = JS_ToCString(ctx, reason);

    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                  "js comcon: a fragment's queued job threw after the fragment "
                  "returned (unhandled rejection): %s", s ? s : "error");

    if (s != NULL) {
        JS_FreeCString(ctx, s);
    }
}


/*
 * COMCON M-CFG (scope isolation). The confined-fragment compartment mirrors the
 * tenant compartment, on its OWN runtime (jcf->comcon_rt) so JS_FreeRuntime
 * tears it down cleanly — a second context on the host runtime leaked
 * (list_empty(gc_obj_list) at the host JS_FreeRuntime). Fragments are held
 * C-side (jcf->comcon_frags) and invoked IN the compartment; only JSON strings
 * cross the boundary (runtime-agnostic), so no JSValue crosses realms/runtimes.
 */
/*
 * Has this request's deadline already passed?  The authoritative test for "the
 * interrupt fired", used instead of matching the exception's message: the
 * message is prose and the deadline is a number, and one of those two is a
 * contract.
 */
static ngx_uint_t
ngx_js_comcon_deadline_passed(ngx_js_worker_t *w)
{
    struct timespec  ts;
    uint64_t         now_ms;

    if (w->request_deadline_ms == 0) {
        return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now_ms = (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;

    return now_ms >= w->request_deadline_ms ? 1 : 0;
}


static JSContext *
ngx_js_comcon_compartment(ngx_js_conf_t *jcf)
{
    JSContext        *sctx;
    ngx_js_worker_t  *w;

    if (jcf->comcon_ctx != NULL) {
        return jcf->comcon_ctx;
    }

    jcf->comcon_rt = JS_NewRuntime();
    if (jcf->comcon_rt == NULL) {
        return NULL;
    }
    JS_SetMemoryLimit(jcf->comcon_rt, 64 * 1024 * 1024);
    JS_SetHostPromiseRejectionTracker(jcf->comcon_rt,
                                      ngx_js_comcon_rejection_tracker, NULL);
    (void) ngx_js_com_register_classes(jcf->comcon_rt);  /* mirror tenant_rt setup */

    /* learn mode: the recorder class must exist in the compartment runtime so
       ngx_js_learn_seed() can seed the withheld host surface with recorders. */
    if (ngx_js_recorder_class_id == 0) {
        JS_NewClassID(&ngx_js_recorder_class_id);
    }
    JS_NewClass(jcf->comcon_rt, ngx_js_recorder_class_id, &ngx_js_recorder_class);

    sctx = ngx_js_tenant_context_new(jcf->comcon_rt);
    if (sctx == NULL) {
        JS_FreeRuntime(jcf->comcon_rt);
        jcf->comcon_rt = NULL;
        return NULL;
    }
    /* cycle for COM getters that read the context opaque; mirror the tenant. */
    JS_SetContextOpaque(sctx, (void *) (uintptr_t) ngx_cycle);

    if (ngx_js_tenant_lockdown(sctx, jcf->comcon_rt, ngx_cycle->log) != NGX_OK) {
        JS_FreeContext(sctx);
        JS_FreeRuntime(jcf->comcon_rt);
        jcf->comcon_rt = NULL;
        return NULL;
    }

    /* Per-context prototypes for the whole COM class set, so a live-cap grant
       (e.g. a re-wrapped socket) is usable in the compartment. Mirrors the
       tenant compartment; installed AFTER lockdown, exactly as the tenant. */
    (void) ngx_js_com_install_protos(sctx);

    /* M-SES-1b: freeze the grantable cap prototypes (install_protos runs after
       the lockdown freeze; without this a granted socket exposes a mutable
       shared proto — cross-fragment pollution). */
    ngx_js_comcon_harden_cap_protos(sctx);

    /* learn mode: seed recorders for the withheld host surface so a confined
       fragment's references to it are HARVESTED (the wishlist) instead of
       failing — the same B0 discovery the tenant compartment provides. NB: the
       compartment is built during the host eval, BEFORE ngx_js_compartment_
       policy_init applies the mode to the process-global, so check jcf->
       tenant_mode (set by comcon.mode()/the directive), not the global. */
    if (jcf->tenant_mode == NGX_JS_TENANT_LEARN) {
        JSValue  cglobal = JS_GetGlobalObject(sctx);
        ngx_js_learn_seed(sctx, cglobal);
        JS_FreeValue(sctx, cglobal);
    }

    jcf->comcon_ctx = sctx;

    /* gas interrupt handler: wire now if the worker exists (post-fork include);
       init-time includes are (re)wired in init_process alongside tenant_rt. */
    w = jcf->worker;
    if (w != NULL) {
        JS_SetInterruptHandler(jcf->comcon_rt, ngx_js_interrupt_handler, w);
    }

    return sctx;
}


/* CONVERGE P3: load a pinned pure-library dependency into the compartment.
 * Reads path, verifies its bytes hash to sha256, evaluates it as a bare-global
 * script (a pure lib reaching for host authority throws — no capability is in
 * scope), and returns its completion value in *out. Same pin-by-hash guarantee
 * as ngx_js_load_tenant_deps, but the value is bound as a fragment closure param
 * (per-fragment) instead of on a shared global. */
static ngx_int_t
ngx_js_comcon_eval_dep(JSContext *ctx, ngx_cycle_t *cycle, ngx_str_t *path,
    const u_char *sha256, JSValue *out, char *reason, size_t rlen)
{
    u_char   *src, digest[32];
    size_t    src_len;
    JSValue   val;

    src = ngx_js_read_file(cycle, path, &src_len);
    if (src == NULL) {
        ngx_snprintf((u_char *) reason, rlen, "cannot read dependency %V%Z",
                     path);
        return NGX_ERROR;
    }

    SHA256(src, src_len, digest);
    if (ngx_memcmp(digest, sha256, 32) != 0) {
        ngx_snprintf((u_char *) reason, rlen, "dependency hash mismatch%Z");
        return NGX_ERROR;
    }

    val = JS_Eval(ctx, (const char *) src, src_len,
                  (const char *) path->data, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JS_FreeValue(ctx, val);
        ngx_snprintf((u_char *) reason, rlen,
                     "dependency is not a pure library%Z");
        return NGX_ERROR;
    }

    *out = val;
    return NGX_OK;
}


/* comcon.__includeConfined(source) -> handle: compile a fragment (function
 * expression) in the compartment; hold it C-side; return an integer handle. */
JSValue
ngx_js_comcon_include_confined(JSContext *hctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    uint32_t  frag_pred;
    ngx_js_conf_t  *jcf;
    JSContext      *sctx;
    JSValue         fn, outer, thrown, exc, name_v, av[16];
#ifdef CONFIG_JIT
    int             aot_c, aot_n;
#endif
    const char     *source, *estr, *name;
    u_char         *buf, *p;
    void           *slot;
    size_t          slen, nlen, total;
    ngx_uint_t      handle;
    uint32_t        gi, gn, dn, di, idx, mask;
    int32_t         sh;
    char            depreason[256];
    ngx_js_compartment_t  tprev;

    jcf = ngx_js_comcon_jcf;
    if (jcf == NULL) {
        return JS_ThrowInternalError(hctx, "comcon.include: no conf");
    }
    sctx = ngx_js_comcon_compartment(jcf);
    if (sctx == NULL) {
        return JS_ThrowInternalError(hctx, "comcon.include: compartment failed");
    }

    source = JS_ToCStringLen(hctx, &slen, argv[0]);
    if (source == NULL) {
        return JS_EXCEPTION;
    }

    /* grant names -> the wrapper's parameter list; grant sockets -> its args,
       each re-wrapped into the compartment (a fresh compartment-native cap
       around the C handle — reach-gated, so it is HELD but its authority is
       still denied by the A1 gate). */
    gn = 0;
    if (argc > 1 && JS_IsObject(argv[1])) {
        name_v = JS_GetPropertyStr(hctx, argv[1], "length");
        JS_ToUint32(hctx, &gn, name_v);
        JS_FreeValue(hctx, name_v);
    }
    /* CONVERGE P3: argv[5] = pinned pure-library deps [{name,path,sha256}].
       Their names join the wrapper's param list after the grants; their eval'd
       values are appended to the closure args, so a dep is bound per-fragment. */
    dn = 0;
    if (argc > 5 && JS_IsObject(argv[5])) {
        name_v = JS_GetPropertyStr(hctx, argv[5], "length");
        JS_ToUint32(hctx, &dn, name_v);
        JS_FreeValue(hctx, name_v);
    }
    if (gn > 16) {
        gn = 16;
    }
    if (gn + dn > 16) {
        dn = 16 - gn;
    }

    /*
     * Build "(function(<grant names><dep names>){\"use strict\";return(...);})".
     *
     * The three pieces are ONE definition each (below, at the top of this file)
     * because the allocation size and the copies used to be separate literals of
     * the same text: editing the wrapper without editing the sizeof overflows
     * this buffer by exactly the difference, and nothing would say so.  Found
     * while writing the D5b-4 negative control that adds a newline here.
     */
    total = sizeof(NGX_JS_COMCON_WRAP_HEAD NGX_JS_COMCON_WRAP_MID
                   NGX_JS_COMCON_WRAP_TAIL) + slen;
    for (gi = 0; gi < gn; gi++) {
        name_v = JS_GetPropertyUint32(hctx, argv[1], gi);
        name = JS_ToCString(hctx, name_v);
        total += (name ? ngx_strlen(name) : 0) + 1;
        if (name) {
            JS_FreeCString(hctx, name);
        }
        JS_FreeValue(hctx, name_v);
    }
    for (di = 0; di < dn; di++) {
        JSValue dv = JS_GetPropertyUint32(hctx, argv[5], di);
        name_v = JS_GetPropertyStr(hctx, dv, "name");
        name = JS_ToCString(hctx, name_v);
        total += (name ? ngx_strlen(name) : 0) + 1;
        if (name) {
            JS_FreeCString(hctx, name);
        }
        JS_FreeValue(hctx, name_v);
        JS_FreeValue(hctx, dv);
    }

    buf = ngx_alloc(total, ngx_cycle->log);
    if (buf == NULL) {
        JS_FreeCString(hctx, source);
        return JS_ThrowOutOfMemory(hctx);
    }
    p = ngx_cpymem(buf, NGX_JS_COMCON_WRAP_HEAD,
                   sizeof(NGX_JS_COMCON_WRAP_HEAD) - 1);
    idx = 0;
    for (gi = 0; gi < gn; gi++) {
        name_v = JS_GetPropertyUint32(hctx, argv[1], gi);
        name = JS_ToCStringLen(hctx, &nlen, name_v);
        if (name != NULL) {
            if (idx > 0) {
                *p++ = ',';
            }
            p = ngx_cpymem(p, name, nlen);
            idx++;
            JS_FreeCString(hctx, name);
        }
        JS_FreeValue(hctx, name_v);
    }
    for (di = 0; di < dn; di++) {
        JSValue dv = JS_GetPropertyUint32(hctx, argv[5], di);
        name_v = JS_GetPropertyStr(hctx, dv, "name");
        name = JS_ToCStringLen(hctx, &nlen, name_v);
        if (name != NULL) {
            if (idx > 0) {
                *p++ = ',';
            }
            p = ngx_cpymem(p, name, nlen);
            idx++;
            JS_FreeCString(hctx, name);
        }
        JS_FreeValue(hctx, name_v);
        JS_FreeValue(hctx, dv);
    }
    p = ngx_cpymem(p, NGX_JS_COMCON_WRAP_MID,
                   sizeof(NGX_JS_COMCON_WRAP_MID) - 1);
    p = ngx_cpymem(p, source, slen);
    p = ngx_cpymem(p, NGX_JS_COMCON_WRAP_TAIL,
                   sizeof(NGX_JS_COMCON_WRAP_TAIL) - 1);
    *p = '\0';                    /* JS_Eval requires a NUL-terminated buffer */
    JS_FreeCString(hctx, source);

    outer = JS_Eval(sctx, (const char *) buf, p - buf,
                    NGX_JS_COMCON_FRAGMENT_ORIGIN, JS_EVAL_TYPE_GLOBAL);
    ngx_free(buf);

    if (JS_IsException(outer)) {
        exc = JS_GetException(sctx);
        estr = JS_ToCString(sctx, exc);
        thrown = JS_ThrowSyntaxError(hctx, "comcon.include: %s",
                                     estr ? estr : "compile error");
        if (estr != NULL) {
            JS_FreeCString(sctx, estr);
        }
        JS_FreeValue(sctx, exc);
        return thrown;
    }

    /*
     * THE HANDLE THIS FRAGMENT WILL BE GIVEN, predicted here because the wrappers
     * are built now and the handle is not assigned until the push at the end --
     * by which time `av[]` has been freed and the wrappers live only inside the
     * closure.
     *
     * A prediction is a coupling, so it is ASSERTED against the real handle after
     * the push rather than trusted: a wrapper bound to the WRONG fragment would
     * be worse than one bound to none, because it would hand one fragment's
     * authority to another under a check that looks like it is working.  If the
     * two ever disagree the fragment is killed rather than published.
     *
     * +1 so that 0 keeps meaning "not bound to any fragment" -- the host's own
     * wrappers, which every gate must keep accepting.
     */
    frag_pred = (jcf->comcon_frags == NULL)
                ? 0 : (uint32_t) jcf->comcon_frags->nelts;

    /* re-wrap each granted cap compartment-native and apply the closure.
       argv[3] is a parallel array of mediate policy descriptors:
         { kind:0, mask }  -> a NginxSocket with a field-redaction mask
         { kind:1, glob }  -> a NginxComFacet (attenuated COM cap) over a
                              granted server, filtered to the route glob.
       Only C-backed identities cross (a socket handle, or the canonical server
       opaque pointer) — never a JSValue. */
    for (gi = 0; gi < gn; gi++) {
        JSValue      cap_v, pol_v;
        int32_t      kind = 0;
        char         bkey[80];
        uint32_t     blimit, bwindow, bttl;
        uint32_t     wdays, wfrom, wto;
        char         ckey[80], cas[48];
        uint32_t     cquorum, cwithin;
        uint8_t      pterm[NGX_JS_PROTO_MAX];
        ngx_uint_t   pn;
        ngx_uint_t   promoted = 0;
        /*
         * Did the grant carry a MEDIATION at all?  The promotion below must not
         * apply to a bare `grants: {out: nginx.outbound()}`: an unmediated
         * outbound grant is refused today, and accepting one would be a widening
         * smuggled in under a bug fix.  The unmediated and the mask-mediated
         * descriptors are otherwise the same shape, so the JS marks the
         * difference rather than the C guessing it.
         */
        int          pol_mediated = 0;

        cap_v = JS_GetPropertyUint32(hctx, argv[2], gi);

        pol_v = JS_UNDEFINED;
        if (argc > 3 && JS_IsObject(argv[3])) {
            pol_v = JS_GetPropertyUint32(hctx, argv[3], gi);
            if (JS_IsObject(pol_v)) {
                name_v = JS_GetPropertyStr(hctx, pol_v, "kind");
                JS_ToInt32(hctx, &kind, name_v);
                JS_FreeValue(hctx, name_v);

                name_v = JS_GetPropertyStr(hctx, pol_v, "mediated");
                pol_mediated = JS_ToBool(hctx, name_v);
                JS_FreeValue(hctx, name_v);
            }
        }

        /* M-LIB `window`: three numbers on the descriptor, read the same way for
         * either capability kind -- one reader so the two cannot disagree about
         * what a window is. */
        wdays = 0; wfrom = 0; wto = 0;
        if (JS_IsObject(pol_v)) {
            JSValue  w_v = JS_GetPropertyStr(hctx, pol_v, "window");
            if (JS_IsObject(w_v)) {
                JSValue  f;
                f = JS_GetPropertyStr(hctx, w_v, "days");
                JS_ToUint32(hctx, &wdays, f);
                JS_FreeValue(hctx, f);
                f = JS_GetPropertyStr(hctx, w_v, "from");
                JS_ToUint32(hctx, &wfrom, f);
                JS_FreeValue(hctx, f);
                f = JS_GetPropertyStr(hctx, w_v, "to");
                JS_ToUint32(hctx, &wto, f);
                JS_FreeValue(hctx, f);
            }
            JS_FreeValue(hctx, w_v);
        }

        /* M-LIB `cosign`: likewise ONE reader for both kinds.  The key is
         * namespaced here rather than in the JS, the way the budget key is: the
         * operator names a DECISION and the C side decides which table that
         * lives in, so a tenant cannot address another subsystem's rows by
         * choosing a clever name. */
        ckey[0] = '\0'; cas[0] = '\0'; cquorum = 0; cwithin = 0;
        if (JS_IsObject(pol_v)) {
            JSValue  c_v = JS_GetPropertyStr(hctx, pol_v, "cosign");
            if (JS_IsObject(c_v)) {
                JSValue      f;
                const char  *cs;

                f = JS_GetPropertyStr(hctx, c_v, "key");
                cs = JS_ToCString(hctx, f);
                if (cs != NULL) {
                    ngx_snprintf((u_char *) ckey, sizeof(ckey) - 1,
                                 "comcon.cosign:%s%Z", cs);
                    JS_FreeCString(hctx, cs);
                }
                JS_FreeValue(hctx, f);

                f = JS_GetPropertyStr(hctx, c_v, "as");
                cs = JS_ToCString(hctx, f);
                if (cs != NULL) {
                    ngx_cpystrn((u_char *) cas, (u_char *) cs, sizeof(cas));
                    JS_FreeCString(hctx, cs);
                }
                JS_FreeValue(hctx, f);

                f = JS_GetPropertyStr(hctx, c_v, "quorum");
                JS_ToUint32(hctx, &cquorum, f);
                JS_FreeValue(hctx, f);

                f = JS_GetPropertyStr(hctx, c_v, "within");
                JS_ToUint32(hctx, &cwithin, f);
                JS_FreeValue(hctx, f);
            }
            JS_FreeValue(hctx, c_v);
        }

        /*
         * WHICH KIND OF CAPABILITY IS THIS REALLY?
         *
         * `kind` on the descriptor says what the MEDIATION was, and `uses`,
         * `ttl` and `cosign` all normalize to an allow-everything MASK -- they
         * attenuate how many times, how long, and by whom, never WHAT -- so they
         * arrive as kind 0, which is the socket shape.  Applied on their own to
         * an OUTBOUND capability that produced
         *
         *     comcon.include: grant is not a NginxSocket or NginxServer
         *
         * for a grant that was a perfectly good outbound capability.  Fail
         * closed, so nothing was ever widened by it -- but the operator was told
         * their capability was the wrong type when the real answer is that the
         * WORD carries no type at all.
         *
         * The kind belongs to the CAPABILITY, not to the word, so it is read
         * back from the capability here rather than trusted from the descriptor.
         * This is the same question `window`'s probe asked one axis over: try
         * each word ALONE, not only in the composition it normally arrives in.
         */
        /* M-LIB `protocol`: the third reader shared by both kinds.  The NAMES
         * cross as data, like every other descriptor field, and are mapped to
         * this capability kind's operation ids here -- the one place that knows
         * which kind the grant turned out to be.  A name this kind does not have
         * cannot arrive (mediate() validates the protocol against the capability,
         * where the operator can be told which word is wrong); the grant is
         * refused rather than partly applied if one ever does, because half a
         * session type enforces an order nobody wrote. */
        pn = 0;
        if (JS_IsObject(pol_v)) {
            JSValue  p_v = JS_GetPropertyStr(hctx, pol_v, "protocol");
            if (JS_IsArray(hctx, p_v)) {
                JSValue     l_v = JS_GetPropertyStr(hctx, p_v, "length");
                uint32_t    pcount = 0, pi;

                JS_ToUint32(hctx, &pcount, l_v);
                JS_FreeValue(hctx, l_v);

                for (pi = 0; pi < pcount && pi < NGX_JS_PROTO_MAX; pi++) {
                    JSValue      t_v = JS_GetPropertyUint32(hctx, p_v, pi);
                    const char  *t = JS_ToCString(hctx, t_v);
                    char         nm[32];
                    size_t       tl;
                    ngx_uint_t   star = 0;
                    ngx_int_t    id;

                    JS_FreeValue(hctx, t_v);
                    if (t == NULL) {
                        break;
                    }

                    tl = ngx_strlen(t);
                    if (tl > 0 && t[tl - 1] == '*') {
                        star = 1;
                        tl--;
                    }
                    if (tl == 0 || tl >= sizeof(nm)) {
                        JS_FreeCString(hctx, t);
                        break;
                    }
                    ngx_memcpy(nm, t, tl);
                    nm[tl] = '\0';
                    JS_FreeCString(hctx, t);

                    id = (kind == 3 || ngx_js_outbound_handle(cap_v) >= 0)
                         ? ngx_js_outbound_op_id(nm)
                         : ngx_js_socket_op_id(nm);
                    if (id == NGX_ERROR) {
                        pn = 0;
                        break;
                    }

                    pterm[pn++] = (uint8_t) (((uint32_t) id << 1) | star);
                }

                if (pn != pcount) {
                    pn = 0;                 /* all or nothing, never half */
                }
            }
            JS_FreeValue(hctx, p_v);
        }

        if (kind == 0 && pol_mediated
            && ngx_js_socket_handle(cap_v) < 0
            && ngx_js_outbound_handle(cap_v) >= 0)
        {
            kind = 3;
            promoted = 1;
        }

        if (kind == 3) {
            /*
             * M-LIB `allowHosts`: the outbound capability, attenuated by a host
             * glob.  Only the C-backed handle crosses -- the glob, the budget
             * and the lifetime are data, so the wrapper on the far side is built
             * from a string and some numbers rather than from anything the host
             * holds.
             */
            int32_t      oh = ngx_js_outbound_handle(cap_v);
            const char  *hglob = NULL;
            size_t       hglen = 0;
            uint32_t     obttl = 0, oblimit = 0, obwindow = 0;
            char         obkey[80];
            /*
             * The glob is copied out of the JS string so that the ONE lifetime
             * rule here stays simple: everything below reads `obglob`, which the
             * C stack owns, and the JS string is released as soon as it has been
             * copied.  The promoted case has no JS string at all, and a shared
             * `const char *` would have made the free at the end conditional on
             * which branch produced it -- a free that depends on provenance is
             * how a literal ends up handed to JS_FreeCString.
             */
            char         obglob[NGX_JS_OUTBOUND_GLOB_LEN];

            if (oh < 0) {
                JS_FreeValue(hctx, pol_v);
                JS_FreeValue(hctx, cap_v);
                goto grant_bad;
            }

            obkey[0] = '\0';
            obglob[0] = '\0';
            name_v = JS_GetPropertyStr(hctx, pol_v, "glob");
            /*
             * JS_IsString FIRST.  JS_ToCStringLen on a MISSING property does not
             * return NULL, it returns the nine-character string "undefined" --
             * so a descriptor with no glob was wrapped with the literal host glob
             * `undefined`, which matches only a host of that name.  Fail-closed
             * by accident, and the refusal below claimed to be the thing that
             * caught it while in fact it never ran for that case.
             */
            if (JS_IsString(name_v)) {
                hglob = JS_ToCStringLen(hctx, &hglen, name_v);
            }

            /*
             * A PROMOTED grant has no glob, because the word that mediated it
             * was not about destinations.  It gets "*", which matches every
             * host -- NOT the empty glob, which the wrapper reads as "this is
             * the host's own unmediated capability".  The difference matters:
             * "*" is a mediated wrapper whose destination set happens to be
             * everything, and it keeps the budget/lifetime/cosignature gates on
             * the path.  An unmediated wrapper would skip them.
             */
            if (promoted && (hglob == NULL || hglen == 0)) {
                if (hglob != NULL) {
                    JS_FreeCString(hctx, hglob);
                }
                JS_FreeValue(hctx, name_v);
                name_v = JS_UNDEFINED;
                hglob = NULL;
                ngx_cpystrn((u_char *) obglob, (u_char *) "*",
                            sizeof(obglob));
            } else if (hglob != NULL && hglen > 0
                       && hglen < sizeof(obglob))
            {
                ngx_cpystrn((u_char *) obglob, (u_char *) hglob,
                            hglen + 1);
            }

            /*
             * A grant with an EMPTY glob is refused rather than wrapped.  An
             * empty glob would reach the far side as "unmediated", which is the
             * host's own shape -- exactly the fail-open that the mediation
             * vocabulary's unknown-flavour refusal exists to prevent.
             */
            if (hglob != NULL) {
                JS_FreeCString(hctx, hglob);
                hglob = NULL;
            }
            JS_FreeValue(hctx, name_v);
            name_v = JS_UNDEFINED;

            if (obglob[0] == '\0') {
                JS_FreeValue(hctx, pol_v);
                JS_FreeValue(hctx, cap_v);
                goto grant_bad;
            }
            hglen = ngx_strlen(obglob);

            {
                JSValue  t_v = JS_GetPropertyStr(hctx, pol_v, "ttlSeconds");
                if (!JS_IsUndefined(t_v)) {
                    JS_ToUint32(hctx, &obttl, t_v);
                }
                JS_FreeValue(hctx, t_v);

                JSValue  b_v = JS_GetPropertyStr(hctx, pol_v, "budget");
                if (JS_IsObject(b_v)) {
                    JSValue  k_v = JS_GetPropertyStr(hctx, b_v, "key");
                    const char *k = JS_ToCString(hctx, k_v);
                    if (k != NULL) {
                        /*
                         * NAMESPACED, exactly as the socket path namespaces it.
                         * It was not, which made uses('k') on a socket and
                         * uses('k') on an outbound capability TWO DIFFERENT
                         * COUNTERS -- against the documented rule that two
                         * capabilities share a budget exactly when the operator
                         * names the same counter.  A budget an operator believed
                         * was one limit of 10 was two limits of 10.
                         */
                        ngx_snprintf((u_char *) obkey, sizeof(obkey) - 1,
                                     "comcon.budget:%s%Z", k);
                        JS_FreeCString(hctx, k);
                    }
                    JS_FreeValue(hctx, k_v);

                    JSValue  l_v = JS_GetPropertyStr(hctx, b_v, "limit");
                    JS_ToUint32(hctx, &oblimit, l_v);
                    JS_FreeValue(hctx, l_v);

                    JSValue  w_v = JS_GetPropertyStr(hctx, b_v, "window");
                    JS_ToUint32(hctx, &obwindow, w_v);
                    JS_FreeValue(hctx, w_v);
                }
                JS_FreeValue(hctx, b_v);
            }

            av[gi] = ngx_js_outbound_wrap(sctx, (uint32_t) oh, obglob, hglen,
                                          obkey[0] ? obkey : NULL,
                                          oblimit, obwindow, obttl);
            ngx_js_outbound_set_window(av[gi], wdays, wfrom, wto);
            ngx_js_outbound_set_cosign(av[gi], ckey, cas, cquorum, cwithin);
            ngx_js_outbound_set_protocol(av[gi], pterm, pn);
            ngx_js_outbound_set_owner(av[gi], frag_pred + 1);
            JS_FreeValue(hctx, pol_v);
            JS_FreeValue(hctx, cap_v);
            if (JS_IsException(av[gi])) {
                goto grant_bad;
            }
            continue;

        } else if (kind == 1) {
            /* route facet over a granted server */
            void        *srv_op = ngx_js_server_srv_op(cap_v);
            const char  *glob = NULL;
            size_t       glen = 0;

            if (srv_op == NULL) {
                JS_FreeValue(hctx, pol_v);
                JS_FreeValue(hctx, cap_v);
                goto grant_bad;
            }
            name_v = JS_GetPropertyStr(hctx, pol_v, "glob");
            glob = JS_ToCStringLen(hctx, &glen, name_v);
            av[gi] = ngx_js_com_facet_wrap(sctx, srv_op, glob ? glob : "*",
                                           glob ? glen : 1);
            ngx_js_com_facet_set_owner(av[gi], frag_pred + 1);
            if (glob != NULL) {
                JS_FreeCString(hctx, glob);
            }
            JS_FreeValue(hctx, name_v);

        } else {
            /* socket, optionally field-masked */
            sh = ngx_js_socket_handle(cap_v);
            if (sh < 0) {
                JS_FreeValue(hctx, pol_v);
                JS_FreeValue(hctx, cap_v);
                goto grant_bad;
            }
            mask = NGX_JS_SOCKET_MASK_ALL;
            bkey[0] = '\0';
            blimit = 0;
            bwindow = 0;
            bttl = 0;

            if (JS_IsObject(pol_v)) {
                JSValue  bud_v;

                name_v = JS_GetPropertyStr(hctx, pol_v, "mask");
                JS_ToUint32(hctx, &mask, name_v);
                JS_FreeValue(hctx, name_v);

                /* M-LIB `uses`: the budget rides the policy descriptor as DATA
                   (key/limit/window), like the mask and the glob -- no JSValue
                   crosses, so the wrapper on the far side is built from numbers
                   and a string rather than from anything the host holds. */
                name_v = JS_GetPropertyStr(hctx, pol_v, "ttlSeconds");
                if (!JS_IsUndefined(name_v)) {
                    JS_ToUint32(hctx, &bttl, name_v);
                }
                JS_FreeValue(hctx, name_v);

                bud_v = JS_GetPropertyStr(hctx, pol_v, "budget");
                if (JS_IsObject(bud_v)) {
                    const char  *bk;

                    name_v = JS_GetPropertyStr(hctx, bud_v, "key");
                    bk = JS_ToCString(hctx, name_v);
                    if (bk != NULL) {
                        ngx_snprintf((u_char *) bkey, sizeof(bkey) - 1,
                                     "comcon.budget:%s%Z", bk);
                        JS_FreeCString(hctx, bk);
                    }
                    JS_FreeValue(hctx, name_v);

                    name_v = JS_GetPropertyStr(hctx, bud_v, "limit");
                    JS_ToUint32(hctx, &blimit, name_v);
                    JS_FreeValue(hctx, name_v);

                    name_v = JS_GetPropertyStr(hctx, bud_v, "window");
                    JS_ToUint32(hctx, &bwindow, name_v);
                    JS_FreeValue(hctx, name_v);
                }
                JS_FreeValue(hctx, bud_v);
            }

            av[gi] = ngx_js_socket_wrap_bounded(sctx, (uint32_t) sh, mask,
                                                bkey[0] ? bkey : NULL,
                                                blimit, bwindow, bttl);
            ngx_js_socket_set_window(av[gi], wdays, wfrom, wto);
            ngx_js_socket_set_cosign(av[gi], ckey, cas, cquorum, cwithin);
            ngx_js_socket_set_protocol(av[gi], pterm, pn);
            ngx_js_socket_set_owner(av[gi], frag_pred + 1);
        }

        JS_FreeValue(hctx, pol_v);
        JS_FreeValue(hctx, cap_v);
        continue;

    grant_bad:
        while (gi-- > 0) {
            JS_FreeValue(sctx, av[gi]);
        }
        JS_FreeValue(sctx, outer);
        return ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_CAP_GRANT,
                   "comcon.include: grant is not a NginxSocket or NginxServer");
    }

    /* eval each pinned dep in the compartment; append its value to the args */
    for (di = 0; di < dn; di++) {
        JSValue      dv, pv, sv;
        const char  *ps, *ss;
        size_t       ps_len, ss_len;
        u_char       sha[32];
        ngx_str_t    dpath;
        ngx_uint_t   b;
        u_char       hi, lo;
        ngx_int_t    rc;

        dv = JS_GetPropertyUint32(hctx, argv[5], di);
        pv = JS_GetPropertyStr(hctx, dv, "path");
        sv = JS_GetPropertyStr(hctx, dv, "sha256");
        ps = JS_ToCStringLen(hctx, &ps_len, pv);
        ss = JS_ToCStringLen(hctx, &ss_len, sv);

        rc = NGX_ERROR;
        depreason[0] = '\0';

        if (ps == NULL || ss == NULL || ss_len != 64) {
            ngx_snprintf((u_char *) depreason, sizeof(depreason),
                         "dependency requires {path, sha256(64 hex)}%Z");
        } else {
            for (b = 0; b < 32; b++) {
                hi = (u_char) ss[b * 2];
                lo = (u_char) ss[b * 2 + 1];
                hi = (hi >= '0' && hi <= '9') ? hi - '0'
                   : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                   : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : 0xff;
                lo = (lo >= '0' && lo <= '9') ? lo - '0'
                   : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                   : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : 0xff;
                if (hi == 0xff || lo == 0xff) {
                    break;
                }
                sha[b] = (u_char) ((hi << 4) | lo);
            }
            if (b < 32) {
                ngx_snprintf((u_char *) depreason, sizeof(depreason),
                             "dependency sha256 has invalid hex%Z");
            } else {
                dpath.len = ps_len;
                dpath.data = ngx_pnalloc(ngx_cycle->pool, ps_len + 1);
                if (dpath.data != NULL) {
                    ngx_memcpy(dpath.data, ps, ps_len);
                    dpath.data[ps_len] = '\0';
                    ngx_cycle_t *cyc = (ngx_cycle_t *) (uintptr_t) ngx_cycle;
                    if (ngx_conf_full_name(cyc, &dpath, 1) == NGX_OK) {
                        rc = ngx_js_comcon_eval_dep(sctx, cyc, &dpath, sha,
                                 &av[gn + di], depreason, sizeof(depreason));
                    }
                }
            }
        }

        if (ps != NULL) { JS_FreeCString(hctx, ps); }
        if (ss != NULL) { JS_FreeCString(hctx, ss); }
        JS_FreeValue(hctx, pv);
        JS_FreeValue(hctx, sv);
        JS_FreeValue(hctx, dv);

        if (rc != NGX_OK) {
            for (b = 0; b < gn + di; b++) {
                JS_FreeValue(sctx, av[b]);
            }
            JS_FreeValue(sctx, outer);
            return ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_ADMIT_DEP,
                     "comcon.include: %s",
                     depreason[0] ? depreason : "dependency load failed");
        }
    }

    fn = JS_Call(sctx, outer, JS_UNDEFINED, (int) (gn + dn),
                 (JSValueConst *) av);
    for (gi = 0; gi < gn + dn; gi++) {
        JS_FreeValue(sctx, av[gi]);
    }
    JS_FreeValue(sctx, outer);

    if (JS_IsException(fn)) {
        return fn;
    }
    if (!JS_IsFunction(sctx, fn)) {
        JS_FreeValue(sctx, fn);
        return ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_ADMIT_SOURCE,
                   "comcon.include: source must be a function expression");
    }

    /* P1 (CONVERGE): compose C3 admission + optional identity pin when the
       contract asks (argv[4] = { imports, checkRequest?, identity? }). Runs on
       the compiled fragment IN the compartment (sctx), where grants are closure
       var-refs (auto-excluded from the free-name check). Learn mode is
       non-enforcing DISCOVERY — skip admission so free names resolve to the
       seeded recorders and are harvested rather than refused. (jcf->tenant_mode,
       not the process-global, since this runs during the host eval — see the
       compartment seed.) */
    if (jcf->tenant_mode != NGX_JS_TENANT_LEARN
        && argc > 4 && JS_IsObject(argv[4]))
    {
        JSValue  imp_h, idv, intr_h;

        imp_h = JS_GetPropertyStr(hctx, argv[4], "imports");
        intr_h = JS_GetPropertyStr(hctx, argv[4], "intrinsics");

        /*
         * The contract asked for admission, so admission RUNS.  This used to be
         * gated on `imports` happening to be an object, and that one condition
         * governed the whole block -- free names, the dynamic-code denial,
         * checkRequest, all of it.  So `include(src, {imports: 42})` compiled a
         * fragment with NO gate at all: measured, `eval("1+1")` was admitted,
         * where {imports: []} refuses it.  A contract that looks stricter than
         * it is, is worse than an absent one.
         *
         * A malformed `imports` now means NO NAMES GRANTED, which is the
         * strictest reading and the fail-closed direction; ngx_js_comcon_
         * admit_check() already treats a non-object that way.
         */
        {
            JSValue      imp_s, intr_s, lv, e, cr;
            uint32_t     ilen = 0, nlen = 0, k;
            const char  *iname;
            int          check;
            char         reason[256];
            ngx_int_t    rc;
            ngx_js_refusal_code_t  rcode;

            imp_s = JS_NewArray(sctx);

            if (JS_IsObject(imp_h)) {
                lv = JS_GetPropertyStr(hctx, imp_h, "length");
                JS_ToUint32(hctx, &ilen, lv);
                JS_FreeValue(hctx, lv);
            }

            for (k = 0; k < ilen; k++) {
                e = JS_GetPropertyUint32(hctx, imp_h, k);
                iname = JS_ToCString(hctx, e);
                JS_SetPropertyUint32(sctx, imp_s, k,
                                     JS_NewString(sctx, iname ? iname : ""));
                if (iname != NULL) {
                    JS_FreeCString(hctx, iname);
                }
                JS_FreeValue(hctx, e);
            }

            cr = JS_GetPropertyStr(hctx, argv[4], "checkRequest");
            check = JS_ToBool(hctx, cr);
            JS_FreeValue(hctx, cr);

            /*
             * The intrinsics NARROWING, copied across contexts like imports.
             * Present-but-not-an-array stays present (an empty sctx array), so
             * a malformed narrowing reads as the strictest setting rather than
             * as absent -- the same fail-closed direction imports takes above.
             */
            intr_s = JS_IsUndefined(intr_h) || JS_IsNull(intr_h)
                     ? JS_UNDEFINED : JS_NewArray(sctx);

            if (!JS_IsUndefined(intr_s) && JS_IsObject(intr_h)) {
                lv = JS_GetPropertyStr(hctx, intr_h, "length");
                JS_ToUint32(hctx, &nlen, lv);
                JS_FreeValue(hctx, lv);

                for (k = 0; k < nlen; k++) {
                    e = JS_GetPropertyUint32(hctx, intr_h, k);
                    iname = JS_ToCString(hctx, e);
                    JS_SetPropertyUint32(sctx, intr_s, k,
                                         JS_NewString(sctx, iname ? iname : ""));
                    if (iname != NULL) {
                        JS_FreeCString(hctx, iname);
                    }
                    JS_FreeValue(hctx, e);
                }
            }

            rc = ngx_js_comcon_admit_check(sctx, fn, imp_s, intr_s, check,
                                           reason, sizeof(reason), &rcode);
            JS_FreeValue(sctx, imp_s);
            JS_FreeValue(sctx, intr_s);

            if (rc != NGX_OK) {
                JS_FreeValue(hctx, imp_h);
                JS_FreeValue(hctx, intr_h);
                JS_FreeValue(sctx, fn);
                return ngx_js_comcon_refuse(hctx, rcode,
                    "comcon.include: admission refused: %s", reason);
            }
        }
        JS_FreeValue(hctx, imp_h);
        JS_FreeValue(hctx, intr_h);

        /* optional identity pin: H(H(source) ‖ schema-version) */
        idv = JS_GetPropertyStr(hctx, argv[4], "identity");
        if (JS_IsString(idv)) {
            static const char  hx[] = "0123456789abcdef";
            const char        *want, *isrc;
            size_t             ilen2;
            SHA256_CTX         ic;
            u_char             chash[32], ident[32], hex[65];
            ngx_uint_t         b;

            isrc = JS_ToCStringLen(hctx, &ilen2, argv[0]);
            if (isrc != NULL) {
                SHA256_Init(&ic);
                SHA256_Update(&ic, isrc, ilen2);
                SHA256_Final(chash, &ic);
                JS_FreeCString(hctx, isrc);

                SHA256_Init(&ic);
                SHA256_Update(&ic, chash, 32);
                SHA256_Update(&ic, (const u_char *) NGX_JS_C4_SCHEMA_VERSION,
                              ngx_strlen(NGX_JS_C4_SCHEMA_VERSION));
                SHA256_Final(ident, &ic);

                for (b = 0; b < 32; b++) {
                    hex[b * 2]     = hx[ident[b] >> 4];
                    hex[b * 2 + 1] = hx[ident[b] & 0xf];
                }
                hex[64] = '\0';

                want = JS_ToCString(hctx, idv);
                if (want == NULL
                    || ngx_strcasecmp((u_char *) want, hex) != 0)
                {
                    if (want != NULL) {
                        JS_FreeCString(hctx, want);
                    }
                    JS_FreeValue(hctx, idv);
                    JS_FreeValue(sctx, fn);
                    return ngx_js_comcon_refuse(hctx,
                        NGX_JS_REFUSAL_PIN_IDENTITY,
                        "comcon.include: artifact identity mismatch");
                }
                JS_FreeCString(hctx, want);
            }
        }
        JS_FreeValue(hctx, idv);
    }

    /* admit phase (iii): run the contract's tests against the fragment IN the
       compartment (under the TENANT reach gate), so its behavior is verified
       with ZERO BLAST RADIUS — the compartment holds no host authority and IO
       is denied. Any test that throws refuses admission. (Determinism caps —
       swapping the clock/RNG for fixed doubles during the run — are a documented
       follow-on; the security-relevant denial, host authority + IO, already
       holds.) contract.tests is a function(fragment){…} source string. */
    if (argc > 4 && JS_IsObject(argv[4])) {
        JSValue  tv = JS_GetPropertyStr(hctx, argv[4], "tests");

        /*
         * PRESENT BUT UNUSABLE IS A REFUSAL, NOT A NO-OP.
         *
         * `tests` is a function(fragment){…} — or its source string. It used to
         * be read with a bare JS_IsString() test and SILENTLY IGNORED otherwise,
         * so the plural spelling the key invites (`tests: [fn]`, an array) was
         * accepted and the behavioural gate never ran: a contract asking to be
         * checked, admitted unchecked, with nothing said. Found by V12's own
         * corpus, whose E_ADMIT_TEST probe was written with an array and was
         * ADMITTED.
         *
         * The direction is the one the `intrinsics` narrowing already takes: a
         * contract that looks stricter than it is, is worse than an absent one.
         */
        if (!JS_IsUndefined(tv) && !JS_IsNull(tv) && !JS_IsString(tv)) {
            JS_FreeValue(hctx, tv);
            JS_FreeValue(sctx, fn);
            return ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_ADMIT_CONTRACT,
                     "comcon.include: admission refused: contract `tests` must "
                     "be a function(fragment) or its source string; refusing "
                     "rather than skipping the test phase");
        }

        if (JS_IsString(tv)) {
            const char  *tsrc;
            size_t       tlen;
            u_char      *tbuf;
            JSValue      testfn, tret;

            tsrc = JS_ToCStringLen(hctx, &tlen, tv);
            if (tsrc != NULL) {
                tbuf = ngx_alloc(tlen + 3, ngx_cycle->log);
                if (tbuf != NULL) {
                    tbuf[0] = '(';
                    ngx_memcpy(tbuf + 1, tsrc, tlen);
                    tbuf[tlen + 1] = ')';
                    tbuf[tlen + 2] = '\0';
                    testfn = JS_Eval(sctx, (const char *) tbuf, tlen + 2,
                                     "<comcon-tests>", JS_EVAL_TYPE_GLOBAL);
                    ngx_free(tbuf);

                    if (JS_IsFunction(sctx, testfn)) {
                        tprev = ngx_js_compartment_enter(
                                    NGX_JS_COMPARTMENT_TENANT);
                        tret = JS_Call(sctx, testfn, JS_UNDEFINED, 1,
                                       (JSValueConst *) &fn);
                        ngx_js_compartment_leave(tprev);

                        if (JS_IsException(tret)) {
                            JSValue      exc2 = JS_GetException(sctx);
                            const char  *es = JS_ToCString(sctx, exc2);

                            JS_FreeValue(sctx, tret);
                            JS_FreeValue(sctx, testfn);
                            JS_FreeCString(hctx, tsrc);
                            JS_FreeValue(hctx, tv);
                            thrown = ngx_js_comcon_refuse(hctx,
                                NGX_JS_REFUSAL_ADMIT_TEST,
                                "comcon.include: admission refused: "
                                "test failed: %s", es ? es : "threw");
                            if (es != NULL) {
                                JS_FreeCString(sctx, es);
                            }
                            JS_FreeValue(sctx, exc2);
                            JS_FreeValue(sctx, fn);
                            return thrown;
                        }
                        JS_FreeValue(sctx, tret);
                    }
                    JS_FreeValue(sctx, testfn);
                }
                JS_FreeCString(hctx, tsrc);
            }
        }
        JS_FreeValue(hctx, tv);
    }

#ifdef CONFIG_JIT
    /* CONVERGE P5: lower the admitted fragment to native C (server-AOT). The
       invoke's JS_Call then dispatches to the compiled jit_func. Confinement is
       preserved by construction — the compiled code calls the same gated host
       functions under the same host-set compartment (the invoke enters TENANT,
       and getters materialize under it). Best-effort: on failure the fragment
       runs interpreted (maxim skips-to-interpreter). */
    if (js_comcon_aot_compile(sctx, fn) == 0) {
        /*
         * D4c: SAY WHAT HAPPENED, not what was attempted.  aot_compile()
         * returns 0 for any bytecode function -- "eligible", never "compiled"
         * (see its header) -- so this used to log "lowered to native C" on
         * every include, including the request-time epoch switches of a live
         * rewrite, where the gcc thread does not exist because it does not
         * survive fork().  The two messages share no substring: a log reader
         * grepping for one can never match the other.
         */
        aot_n = 0;
        aot_c = js_comcon_aot_status(sctx, fn, &aot_n);
        if (aot_c > 0) {
            ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                          "js comcon: include fragment NATIVE "
                          "(COMCON C5 server-AOT: %d of %d functions)",
                          aot_c, aot_n);
        } else {
            ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0,
                          "js comcon: include fragment BYTECODE "
                          "(%d functions, nothing lowered: no compiler in "
                          "this process)", aot_n);
        }
    }
#endif

    if (jcf->comcon_frags == NULL) {
        /* Own the backing on a dedicated pool: this array grows at REQUEST time
           (D4a rebuild-on-write), but the first include may run during host-JS
           eval (config time) when ngx_cycle->pool is a transient config pool —
           growing it later on that stale pool corrupts memory. A standalone pool
           lives until teardown, independent of the cycle. */
        jcf->comcon_frags_pool = ngx_create_pool(4096, ngx_cycle->log);
        if (jcf->comcon_frags_pool == NULL) {
            JS_FreeValue(sctx, fn);
            return JS_ThrowOutOfMemory(hctx);
        }
        jcf->comcon_frags = ngx_array_create(jcf->comcon_frags_pool, 8,
                                             sizeof(JSValue));
        if (jcf->comcon_frags == NULL) {
            JS_FreeValue(sctx, fn);
            return JS_ThrowOutOfMemory(hctx);
        }
    }
    slot = ngx_array_push(jcf->comcon_frags);
    if (slot == NULL) {
        JS_FreeValue(sctx, fn);
        return JS_ThrowOutOfMemory(hctx);
    }
    *(JSValue *) slot = fn;                          /* the array owns fn */
    handle = jcf->comcon_frags->nelts - 1;

    /*
     * The prediction, checked.  Nothing between the grant loop and here pushes to
     * this array today, so this cannot fire -- which is exactly why it is written
     * down: if some future path does, every wrapper this include built is bound to
     * a DIFFERENT fragment's handle, and the owner gate would then be enforcing an
     * invariant nobody holds.  The fragment is killed rather than published, and
     * the handle refuses as a stale epoch.
     */
    if ((uint32_t) handle != frag_pred) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "js comcon: fragment handle %L is not the predicted %uD; "
                      "its granted capabilities are bound to the wrong fragment "
                      "and it has been discarded",
                      (int64_t) handle, frag_pred);
        JS_FreeValue(sctx, fn);
        *(JSValue *) slot = JS_UNDEFINED;
        return ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_CAP_GRANT,
                   "comcon.include: granted capabilities could not be bound to "
                   "this fragment");
    }

    return JS_NewInt64(hctx, (int64_t) handle);
}


/*
 * comcon.__aotStatus(handle) -> {jit, functions, compiled}
 *
 * COMCON D4c: which TIER is this fragment's code actually running on, right now.
 *
 * Needed because nothing could answer it.  js_comcon_aot_compile() returns 0 for
 * any bytecode function, so "did the lowering happen" was unanswerable, and
 * POM.md §4's "bytecode fallback -> re-AOT -> live(e+1)" was prose no test could
 * check.  It matters most exactly where the answer is least obvious: a live
 * epoch switch runs in a WORKER, post-fork, where there is no gcc thread (it
 * does not survive fork()), so a rewritten fragment runs BYTECODE until a
 * process that has a compiler compiles it.  That is a real property of the
 * architecture, and an operator doing a live rewrite should be able to see it
 * rather than infer it.
 *
 * Reads only: it never compiles, and on a build without CONFIG_JIT it reports
 * jit:false rather than pretending there is a tier to report on.
 */
JSValue
ngx_js_comcon_aot_status(JSContext *hctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_conf_t  *jcf;
    JSValueConst    fn;
    JSValue         r;
    int64_t         handle = 0;
#ifdef CONFIG_JIT
    int             n = 0, c;
#endif

    jcf = ngx_js_comcon_jcf;
    if (jcf == NULL || jcf->comcon_ctx == NULL || jcf->comcon_frags == NULL) {
        return JS_ThrowInternalError(hctx, "comcon: no compartment");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(hctx, "comcon.__aotStatus: handle required");
    }
    JS_ToInt64(hctx, &handle, argv[0]);

    if (handle < 0 || (ngx_uint_t) handle >= jcf->comcon_frags->nelts) {
        return JS_ThrowTypeError(hctx, "comcon: bad fragment handle");
    }
    fn = ((JSValue *) jcf->comcon_frags->elts)[handle];   /* borrowed */

    if (JS_IsUndefined(fn)) {
        return JS_ThrowTypeError(hctx,
                                 "comcon: fragment was freed (stale epoch)");
    }

    r = JS_NewObject(hctx);
    if (JS_IsException(r)) {
        return r;
    }

#ifdef CONFIG_JIT
    c = js_comcon_aot_status(jcf->comcon_ctx, fn, &n);
    JS_SetPropertyStr(hctx, r, "jit", JS_TRUE);
    JS_SetPropertyStr(hctx, r, "functions", JS_NewInt32(hctx, n));
    JS_SetPropertyStr(hctx, r, "compiled", JS_NewInt32(hctx, c < 0 ? 0 : c));
#else
    JS_SetPropertyStr(hctx, r, "jit", JS_FALSE);
    JS_SetPropertyStr(hctx, r, "functions", JS_NewInt32(hctx, 0));
    JS_SetPropertyStr(hctx, r, "compiled", JS_NewInt32(hctx, 0));
#endif

    return r;
}


/*
 * G6.16, THE ACCOUNTING HALF: A FRAGMENT'S LEFTOVERS ARE DRAINED FIRST, AND
 * CHARGED TO NOBODY.
 *
 * Every invocation drains the compartment to quiescence before it returns, so a
 * fragment's continuations are charged to, and gated at, the fragment that
 * created them.  That drain is BEST-EFFORT: a fragment which outruns the job
 * budget leaves work queued, and nothing can un-queue ordinary JS.
 *
 * The AUTHORITY half of what remained is closed -- `cap.owner` refuses a
 * capability to anyone but the fragment it was granted to, so a leftover job
 * runs and obtains nothing.  THE ACCOUNTING HALF WAS NOT, and it is a channel in
 * both directions:
 *
 *   - THE BUDGET.  Leftovers were drained by the next invocation's trailing
 *     loop, out of the next fragment's job budget.  Measured: A leaves 10,100
 *     jobs behind, B then queues 100 of its own, and B's continuations DO NOT RUN
 *     AT ALL -- B's whole 10,000-job allowance goes on a stranger's work, and B's
 *     capability records nothing.  A fragment could therefore silence the next
 *     fragment's continuations, which is the original escape's shape (one
 *     fragment reaching into another's invocation) with the arrow reversed.
 *   - THE CLOCK.  They ran on the next fragment's deadline, so an innocent
 *     request paid for them in latency and could be stopped by its own meter
 *     over work it did not queue.
 *   - THE REPORT.  The unhandled-rejection counter is reset per invocation, so a
 *     leftover that rejected was logged as "this fragment's queued jobs" against
 *     a fragment that had never seen it.  A log line that names the wrong
 *     fragment is worse than no line: it sends an operator to the wrong author.
 *
 * So the leftovers are drained HERE, before the invocation arms its deadline,
 * narrows its allowance, pushes its posture or claims its identity -- under a
 * separate job budget, a separate short deadline, the FLEET posture rather than
 * any binding's, and `cur_frag = 0`, which is nobody.
 *
 * IT RUNS AS NOBODY, AND BY CONSTRUCTION RATHER THAN BY ASSIGNMENT.  `cur_frag`
 * is zero outside any invocation, and the nested-invoke guard below is what
 * establishes that it is zero here -- so there is no frag_set() in this function,
 * because there is nothing to set.  (A line that assigns the value a guard has
 * just proved is code no control can break, which this tree deletes.)  That
 * identity is what makes this the same answer as before rather than a new hole:
 * `ngx_js_cap_foreign()` is `owner != 0 && owner != cur_frag`, so every
 * fragment-granted capability is foreign to nobody and `cap.owner` denies it --
 * exactly as it did when the leftovers ran inside a stranger.  The authority
 * outcome is unchanged; only the bill moves.
 *
 * IT RUNS INSIDE THE COMPARTMENT, and that is not optional.  These jobs are
 * fragment code; running them between compartment scopes would run them as
 * HOST_ROOT with the A1 reach gate switched off, which would convert an
 * accounting fix into the escape it is supposed to be tidying up after.
 *
 * A LEFTOVER THAT COULD NOT RUN DOES NOT FAIL THIS INVOCATION.  The trailing
 * drain re-raises such a job, because there it means the fragment being invoked
 * did not finish.  Here it means a PREVIOUS fragment did not finish, which the
 * current one is not answerable for: it is logged as a leftover and the
 * invocation proceeds.  Failing the innocent caller would be the accounting
 * defect again, dressed as strictness.
 *
 * Nested invokes are skipped (`cur_frag != 0`): inside a fragment the pending
 * jobs may be that fragment's own, and running them as nobody would deny
 * capabilities that are legitimately theirs.  Nothing reaches a nested invoke
 * today; the guard costs a comparison and removes the need to remember that.
 */
static void
ngx_js_comcon_drain_leftovers(ngx_js_conf_t *jcf, ngx_js_worker_t *w)
{
    JSMemoryUsage         mu;
    struct timespec       ts;
    uint64_t              saved_deadline = 0, now_ms, newd;
    ngx_uint_t            jobs = 0, failed = 0, rejections;
    ngx_uint_t            deadline_pushed = 0;
    ngx_js_compartment_t  prev;

    if (!JS_IsJobPending(jcf->comcon_rt)) {
        return;
    }

    if (ngx_js_compartment_frag_get() != 0) {
        return;                          /* nested: not ours to reassign */
    }

    /*
     * The same per-invocation allowance a fragment gets, so a leftover chain
     * that keeps allocating cannot eat the shared runtime while pretending to
     * belong to nobody.  Restored to the compartment's own limit afterwards,
     * exactly as the invocation path restores it.
     */
    JS_ComputeMemoryUsage(jcf->comcon_rt, &mu);
    JS_SetMemoryLimit(jcf->comcon_rt,
                      (size_t) mu.malloc_size
                      + (size_t) NGX_JS_COMCON_FRAGMENT_MEMORY_BYTES);

    if (w != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now_ms = (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;
        newd = now_ms + NGX_JS_COMCON_LEFTOVER_MS;
        saved_deadline = w->request_deadline_ms;

        /* only ever TIGHTEN: the request's own deadline stays the outer bound */
        w->request_deadline_ms =
            (saved_deadline != 0 && saved_deadline < newd) ? saved_deadline
                                                           : newd;
        deadline_pushed = 1;
    }

    /* Whatever this drain's jobs reject is reported as the LEFTOVERS' -- the
       invocation resets these again for its own drain. */
    rejections = ngx_js_comcon_rejections;
    ngx_js_comcon_rejections = 0;
    ngx_js_comcon_rejections_logged = 0;

    prev = ngx_js_compartment_enter(NGX_JS_COMPARTMENT_TENANT);

    while (JS_IsJobPending(jcf->comcon_rt)
           && jobs < NGX_JS_COMCON_MAX_LEFTOVER_JOBS)
    {
        JSContext  *jctx = NULL;
        int         jrc;

        jrc = JS_ExecutePendingJob(jcf->comcon_rt, &jctx);

        if (jrc == 0) {
            break;
        }

        jobs++;

        if (jrc < 0 && jctx != NULL) {
            JS_FreeValue(jctx, JS_GetException(jctx));
            failed++;
        }
    }

    ngx_js_compartment_leave(prev);

    if (deadline_pushed) {
        w->request_deadline_ms = saved_deadline;
    }

    JS_SetMemoryLimit(jcf->comcon_rt, 64 * 1024 * 1024);

    /*
     * REPORTED AS LEFTOVERS, which is the whole point of draining them here.
     * One line, naming what they are, so an operator reading it is not sent to
     * the author of the fragment that merely arrived next.
     */
    ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                  "js comcon: drained %ui leftover job(s) from an earlier "
                  "fragment before this invocation (%ui could not run, %ui "
                  "rejected unhandled, %s)",
                  jobs, failed, ngx_js_comcon_rejections,
                  JS_IsJobPending(jcf->comcon_rt)
                      ? "more remain for the next invocation"
                      : "the compartment is now quiescent");

    ngx_js_comcon_rejections = rejections;
    ngx_js_comcon_rejections_logged = 0;
}


/* comcon.__invokeConfined(handle, arg, timeoutMs) -> result: invoke the held
 * fragment in the compartment; `arg`/result marshaled by JSON round-trip in C
 * (only strings cross). Metered via the worker deadline (compartment runtime
 * has the same interrupt handler). */
JSValue
ngx_js_comcon_invoke_confined(JSContext *hctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_js_conf_t    *jcf;
    JSContext        *sctx;
    ngx_js_worker_t  *w = NULL;
    JSValueConst      fn;
    JSValue           arg, result, jstr, retv, exc, stack_v;
    const char       *s, *st, *where;
    char              wbuf[24];
    int               wn = 0;
    size_t            len;
    int64_t           handle = 0;
    int               nargs = 0;
    uint32_t          timeout = 0;
    uint32_t          memory = 0;
    uint32_t          onviol = 0;
    int               settled = 0;
    ngx_uint_t        pending = 0;
    ngx_uint_t        jobs = 0;
    ngx_uint_t        job_errors = 0;
    ngx_uint_t        job_failed = 0;
    JSValue           irq = JS_UNDEFINED;
    uint32_t          saved_frag = 0;
    ngx_js_tenant_mode_e  saved_mode = NGX_JS_TENANT_ENFORCE;
    ngx_uint_t        mode_pushed = 0;
    uint64_t          old_deadline = 0, now_ms, newd;
    ngx_uint_t        metered = 0;
    JSMemoryUsage     mu;
    struct timespec   ts;
    ngx_js_compartment_t  prev;

    jcf = ngx_js_comcon_jcf;
    if (jcf == NULL || jcf->comcon_ctx == NULL || jcf->comcon_frags == NULL) {
        return JS_ThrowInternalError(hctx, "comcon: no compartment");
    }
    sctx = jcf->comcon_ctx;

    JS_ToInt64(hctx, &handle, argv[0]);
    if (argc > 2) {
        JS_ToUint32(hctx, &timeout, argv[2]);
    }
    if (argc > 3) {
        JS_ToUint32(hctx, &memory, argv[3]);
    }
    if (argc > 4) {
        JS_ToUint32(hctx, &onviol, argv[4]);
    }

    if (handle < 0 || (ngx_uint_t) handle >= jcf->comcon_frags->nelts) {
        return JS_ThrowTypeError(hctx, "comcon: bad fragment handle");
    }
    fn = ((JSValue *) jcf->comcon_frags->elts)[handle];   /* borrowed */

    if (JS_IsUndefined(fn)) {
        /* freed by __freeConfined (a superseded epoch beyond the rollback
           window); invoking a stale handle is an error, not a crash. */
        return ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_EPOCH_STALE,
                   "comcon: fragment was freed (stale epoch)");
    }

    arg = JS_UNDEFINED;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        jstr = JS_JSONStringify(hctx, argv[1], JS_UNDEFINED, JS_UNDEFINED);
        s = JS_ToCStringLen(hctx, &len, jstr);
        if (s != NULL) {
            arg = JS_ParseJSON(sctx, s, len, "<arg>");
            JS_FreeCString(hctx, s);
            nargs = 1;
        }
        JS_FreeValue(hctx, jstr);
    }

    /*
     * G6.16 accounting: finish an EARLIER fragment's leftovers before this
     * invocation arms anything of its own, so they cannot be paid for out of
     * this fragment's job budget, deadline, allowance or rejection report.  See
     * ngx_js_comcon_drain_leftovers() for why they run as nobody.
     */
    ngx_js_comcon_drain_leftovers(jcf, jcf->worker);

    /*
     * A confined fragment ALWAYS runs under a deadline.
     *
     * The contract's meter sets it; with no meter, `timeout` arrives as 0 and
     * this used to arm nothing -- so the default configuration ran untrusted
     * code with no bound at all, and an accidental infinite loop took the
     * worker down without any escape being involved. The M-SES gate's
     * resource-guard condition would still pass, because it arms the deadline
     * itself; that gap is what made this worth fixing rather than documenting.
     *
     * Enforced HERE rather than in the JS wrapper that computes `ms`, so
     * calling __invokeConfined directly cannot skip it.
     *
     * An explicit meter still wins when it asks for LONGER, and the min() below
     * keeps the old rule that a fragment may only tighten an enclosing
     * deadline, never extend it.
     */
    if (timeout == 0) {
        timeout = NGX_JS_COMCON_FRAGMENT_TIMEOUT_MS;
    }

    w = jcf->worker;
    if (w != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now_ms = (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;
        newd = now_ms + timeout;
        old_deadline = w->request_deadline_ms;
        w->request_deadline_ms =
            (old_deadline != 0 && old_deadline < newd) ? old_deadline : newd;
        metered = 1;
    }

    /*
     * F2: a per-INVOCATION memory allowance, enforced by narrowing the runtime
     * limit for the duration of this call and restoring it afterwards. The
     * invoke is single-threaded, so growth in that window is attributable to
     * this fragment -- which is as much attribution as one shared runtime can
     * honestly give.
     *
     * A contract may only NARROW: the min() is what stops __invokeConfined from
     * being called directly with a larger allowance than the default, the same
     * rule the deadline above follows.
     */
    if (memory == 0 || memory > NGX_JS_COMCON_FRAGMENT_MEMORY_BYTES) {
        memory = NGX_JS_COMCON_FRAGMENT_MEMORY_BYTES;
    }

    JS_ComputeMemoryUsage(jcf->comcon_rt, &mu);
    JS_SetMemoryLimit(jcf->comcon_rt,
                      (size_t) mu.malloc_size + (size_t) memory);

    /* run the fragment as a confined compartment: the A1 reach gate denies the
       authority edges (e.g. a granted socket's .listener) even though the
       fragment legitimately holds the cap. */
    /*
     * M-LIB `onViolation`: a PER-BINDING audit/enforce mode, in force for the
     * duration of this one invocation and restored afterwards.
     *
     * The fleet-wide switch (comcon.mode) is the wrong granularity for the
     * rollout MANUAL describes: shadowing one tenant's new policy by putting the
     * fleet in audit ALSO stops enforcing every other tenant's, which is a
     * strictly worse posture than the one the operator is trying to reach
     * carefully.  Observe-first has to be a property of the BINDING.
     *
     * It can WEAKEN as well as strengthen, and that is only acceptable because
     * the contract is written on the trusted side -- the same argument cosign's
     * `as` rests on.  The fragment's SOURCE is untrusted; the contract around it
     * is the operator's own configuration, and an operator asking for shadow
     * mode on one binding is asking for exactly what the word says.
     *
     * Restored unconditionally below, including on the exception path: a
     * fragment that throws must not leave the worker in the mode its own
     * contract asked for.
     */
    if (onviol > 0) {
        saved_mode = ngx_js_compartment_mode_get();
        ngx_js_compartment_mode_set(
            (onviol == 1) ? NGX_JS_TENANT_AUDIT
                          : ((onviol == 3) ? NGX_JS_TENANT_LEARN
                                           : NGX_JS_TENANT_ENFORCE));
        mode_pushed = 1;
    }

    /*
     * WHICH FRAGMENT IS RUNNING, for the whole call INCLUDING THE DRAIN.  A
     * granted wrapper records the fragment it was granted to, and the gates
     * refuse it to anyone else -- so a continuation that outlives its own
     * invocation and runs inside a stranger's holds capabilities it cannot use.
     *
     * Saved and restored rather than assigned, so a nested invoke would inherit
     * correctly; nothing reaches one today, and this costs one word either way.
     */
    saved_frag = ngx_js_compartment_frag_get();
    ngx_js_compartment_frag_set((uint32_t) handle + 1);

    prev = ngx_js_compartment_enter(NGX_JS_COMPARTMENT_TENANT);
    result = JS_Call(sctx, fn, JS_UNDEFINED, nargs, (JSValueConst *) &arg);

    /*
     * AN ASYNC FRAGMENT RETURNS A PROMISE, and a promise has to be settled
     * before anything can be marshalled out of it.
     *
     * The compartment has its OWN runtime, so draining its pending jobs runs the
     * fragment's microtasks and nothing else -- no host job, no other tenant's
     * continuation, can be scheduled by this loop.  That isolation is the reason
     * this is safe to do at all, and it is worth stating because the same loop
     * over the host runtime would be a very different thing.
     *
     * IT DRAINS MICROTASKS, NOT THE WORLD.  A promise that only a timer or an
     * outbound response could settle stays PENDING however long this runs, and
     * is reported as such rather than waited on: there is nothing to wait for.
     * That is the honest shape of "async fragments" here, and it is exactly why
     * `allowHosts` records intent instead of fetching.
     *
     * Two bounds, and both are needed -- and which of them fires was written
     * down wrongly at v5.92 and corrected by measurement at v5.93.  The claim
     * was that the DEADLINE is the real one and the job cap is the belt.  For a
     * promise-chain runaway it is the other way round: 354,885 promises exhaust
     * the F2 per-invocation MEMORY ALLOWANCE long before a 300 ms deadline
     * elapses, and with no cap that is what ends the loop.  So the cap is what
     * makes THIS loop terminate promptly with a usable message ("your promise
     * never settled" rather than a timeout), the allowance is what stops an
     * uncapped one, and the deadline is the outer bound on all of it.  A bound
     * nobody measured is a bound nobody knows the order of.
     */
    if ((int) JS_PromiseState(sctx, result) != -1) {

        while (JS_PromiseState(sctx, result) == JS_PROMISE_PENDING
               && JS_IsJobPending(jcf->comcon_rt)
               && jobs < NGX_JS_COMCON_MAX_JOBS)
        {
            JSContext  *jctx = NULL;

            if (JS_ExecutePendingJob(jcf->comcon_rt, &jctx) <= 0) {
                break;          /* no job ran, or one threw: stop draining */
            }
            jobs++;
        }

        settled = (int) JS_PromiseState(sctx, result);

        if (settled == JS_PROMISE_PENDING) {
            JS_FreeValue(sctx, result);
            result = JS_UNDEFINED;
            pending = 1;

        } else {
            JSValue  v = JS_PromiseResult(sctx, result);

            JS_FreeValue(sctx, result);

            if (settled == JS_PROMISE_REJECTED) {
                /* A rejection is the async spelling of a throw, so it takes the
                 * throw's path -- including the fragment-origin extraction
                 * below, which an operator needs more for an async fragment than
                 * for a synchronous one. */
                result = JS_Throw(sctx, v);
            } else {
                result = v;
            }
        }
    }

    /*
     * QUIESCENCE, AND WHY IT IS AN INVARIANT RATHER THAN A TIDY-UP.
     *
     * A fragment can queue a job and return without awaiting it:
     *
     *     function(a){ Promise.resolve().then(function(){
     *                      out.request('https://a.example.com/LATE'); });
     *                  return 'returned'; }
     *
     * Nothing else in this process drains `comcon_rt` -- the host runtime's
     * drains are a different runtime -- so that job used to sit pending until
     * some LATER, UNRELATED invocation returned a promise, and then ran inside
     * it.  Measured: the capability was untouched when the fragment returned and
     * exercised during the next fragment's settle loop.
     *
     * Everything an invocation bounds was therefore the wrong invocation's.  The
     * deferred use ran on a stranger's DEADLINE and MEMORY ALLOWANCE; it was
     * gated at a stranger's wall-clock time, so `ttl` and `window` were
     * evaluated at the wrong moment; and it ran under a stranger's `onViolation`
     * POSTURE, so a shadowed fragment's deferred work could execute under an
     * enforcing binding, or an enforced fragment's under audit.  It is also a
     * channel: the first fragment spends the second one's job budget.
     *
     * This was unreachable before async fragments were admitted, because a
     * fragment that could not name `Promise` could not queue a job.  Widening
     * admission opened it, so closing it belongs with that change.
     *
     * So: every invocation drains to QUIESCENCE, here, inside the compartment
     * scope and before the posture and the memory limit are restored.  A
     * fragment's continuations are then charged to, and gated at, the fragment
     * that created them.
     *
     * BOUNDED BY THE SAME JOB BUDGET AS THE SETTLE LOOP -- one budget for the
     * whole invocation, so a fragment cannot draw twice the allowance by
     * returning a promise.  No test distinguishes one shared budget from two
     * today, because both stop promptly; recorded here rather than implied.
     *
     * The first version of this deliberately had no cap,
     * on the argument that an invariant with a cap is not an invariant.
     *
     * That argument is correct, and the consequence was not affordable.  A
     * self-queueing chain that does not exhaust memory quickly then drains until
     * the REQUEST's deadline: measured, a fragment that used to be refused in
     * milliseconds became a ten-second request the client abandoned, caught by
     * the full suite rather than by the test written for this change.
     *
     * So quiescence here is BEST-EFFORT, and that is stated rather than implied.
     * The budget is large enough that any fragment whose continuations are
     * bounded is fully attributed, which is every fragment that is not
     * deliberately pathological; one that exceeds it is REPORTED, loudly, and its
     * leftover jobs can still run inside a later invocation.
     *
     * BOTH HALVES OF WHAT THIS LOOP LEAVES BEHIND ARE NOW PAID.  The AUTHORITY
     * half is `cap.owner`: every granted wrapper is bound to its fragment and the
     * gates refuse it to anyone else, so a leftover job runs and obtains nothing.
     * The ACCOUNTING half is ngx_js_comcon_drain_leftovers(), which finishes them
     * at the START of the next invocation under bounds of their own -- so this
     * loop is about ATTRIBUTION only, which is all a best-effort loop can
     * honestly promise.
     *
     * A job that THROWS does not fail the invocation: the fragment's value was
     * already computed and returned legitimately.  It is logged -- once, with a
     * count -- because a fragment whose continuations fail is a fragment whose
     * author needs to know, and because one line per invocation cannot be turned
     * into a log flood by queueing ten thousand throwing jobs.
     *
     * Draining continues past a throwing job rather than breaking: stopping
     * there would leave the rest pending, which is the escape again.
     */
    if (JS_IsException(result)) {
        /*
         * Stash the exception across the drain.  A job runs arbitrary fragment
         * code on this context and would otherwise clobber the pending
         * exception, turning a fragment that threw into one that returned.
         */
        exc = JS_GetException(sctx);
    } else {
        exc = JS_UNDEFINED;
    }

    ngx_js_comcon_rejections = 0;
    ngx_js_comcon_rejections_logged = 0;

    while (JS_IsJobPending(jcf->comcon_rt)
           && jobs < NGX_JS_COMCON_MAX_JOBS)
    {
        JSContext  *jctx = NULL;
        int         jrc;

        jrc = JS_ExecutePendingJob(jcf->comcon_rt, &jctx);

        if (jrc == 0) {
            break;                          /* nothing left to run */
        }

        jobs++;

        if (jrc < 0 && jctx != NULL) {
            JSValue  jexc = JS_GetException(jctx);

            /*
             * A JOB THAT COULD NOT RUN MEANS THE FRAGMENT DID NOT FINISH, and
             * the invocation says so.  The first version of this loop logged and
             * carried on, and then returned the value the fragment had already
             * computed -- so a fragment stopped by its own meter reported
             * SUCCESS.  Found by the `/forever` probe reporting `returned` where
             * it had to report `stopped`.
             *
             * THE RULE IS "COULD NOT RUN", NOT "THREW", and the difference is the
             * thing that was got wrong twice.  A promise reaction that throws
             * does NOT fail: the promise machinery catches it and rejects the
             * derived promise, so the job succeeds and the failure surfaces as an
             * unhandled rejection (reported by the tracker, and NOT fatal -- the
             * fragment's value was computed legitimately).  A negative return is
             * something else entirely: the platform stopped the fragment.  In
             * this compartment the only jobs are promise reactions, so that means
             * the deadline or the memory allowance -- both of which the operator
             * set, and neither of which may be reported as success.
             *
             * Draining CONTINUES rather than breaking, deliberately: a job that
             * could not run cannot enqueue another, so the queue still drains to
             * empty and quiescence holds.  Breaking here would leave jobs pending
             * for a later, unrelated invocation, which is the escape this loop
             * exists to close.  Both properties are kept by finishing the drain
             * and re-raising afterwards.
             */
            /*
             * NO REACHABLE JOB PRODUCES THIS TODAY, and the code stays anyway --
             * a deliberate exception to "delete what no control can break", for
             * the reason the include-translation fall-through is kept.
             *
             * Every job in this compartment is a promise reaction, and a promise
             * reaction that throws does NOT return failure: the machinery catches
             * it and rejects the derived promise, so the failure surfaces as an
             * unhandled rejection instead (the tracker reports those, and they
             * are NOT fatal -- the fragment's value was computed legitimately).
             * Measured: a promise chain that exhausts its memory allowance ran
             * 354,885 jobs and then ended with ONE unhandled rejection and zero
             * failed jobs.
             *
             * What this guards is a future job source that is not a promise
             * reaction -- a timer, a queueMicrotask, a worker message.  For such
             * a job a negative return means the platform stopped the fragment,
             * and the invocation must not report success: the first version of
             * this loop logged and carried on, and a fragment stopped by its own
             * meter reported the value it had already computed.
             */
            if (!job_failed) {
                const char  *jm = JS_ToCString(jctx, jexc);

                job_failed = 1;
                irq = jexc;                  /* re-raised after the drain */

                ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                              "js comcon: a fragment's queued job could not run "
                              "(%s): %s",
                              (metered && w != NULL
                               && ngx_js_comcon_deadline_passed(w))
                              ? "its deadline had passed"
                              : "the fragment's allowance was exhausted",
                              jm ? jm : "error");

                if (jm != NULL) {
                    JS_FreeCString(jctx, jm);
                }

            } else {
                job_errors++;
                JS_FreeValue(jctx, jexc);
            }
        }
    }

    if (job_errors > 0) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "js comcon: %ui more of this fragment's queued jobs could "
                      "not run", job_errors);
    }

    /*
     * The loud half of "best effort".  A fragment that outran the job budget
     * leaves work behind; silence here would be the original defect with extra
     * steps.  What happens to that work is no longer "it runs inside a later
     * invocation, on its deadline and under its posture" -- the next invocation
     * finishes it FIRST, as nobody, under a budget and a deadline of its own
     * (ngx_js_comcon_drain_leftovers) -- and the message says so, because an
     * operator reading it needs to know who will be billed.
     */
    if (JS_IsJobPending(jcf->comcon_rt)) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "js comcon: a fragment outran the %ui-job budget and left "
                      "queued jobs behind; the next invocation will finish them "
                      "first, as nobody, under bounds of their own",
                      (ngx_uint_t) NGX_JS_COMCON_MAX_JOBS);
    }

    if (ngx_js_comcon_rejections > 1) {
        ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                      "js comcon: %ui of this fragment's queued jobs rejected "
                      "unhandled after it returned",
                      ngx_js_comcon_rejections);
    }

    if (job_failed) {
        /*
         * The fragment did not finish: that outranks whatever it had managed to
         * compute, and it outranks an exception it threw on its way out too.
         */
        if (!JS_IsUndefined(exc)) {
            JS_FreeValue(sctx, exc);
        }
        if (!JS_IsException(result)) {
            JS_FreeValue(sctx, result);
        }
        result = JS_Throw(sctx, irq);        /* consumes irq */

    } else if (!JS_IsUndefined(exc)) {
        result = JS_Throw(sctx, exc);        /* consumes exc */
    }

    if (JS_IsException(result)) {
        exc = JS_GetException(sctx);
        s = JS_ToCString(sctx, exc);

        /*
         * D5b-4 (cross-file provenance): carry the fragment's OWN location out
         * with the error.  Without it a fragment failure says what went wrong
         * and not WHERE, so MANUAL §7.4's denial-record `where` (file:line)
         * cannot be filled for the one tier where it matters most.
         *
         * Only the "<comcon-fragment>:LINE[:COL]" token is copied -- never the
         * rest of the stack, which also names host frames and host paths.  The
         * fragment's synthetic file origin is that name; the line is the
         * AUTHOR's line, because include()'s wrapper preamble
         * ("(function(g){\"use strict\";return(") contains no newline.  That
         * is a contract, not a coincidence: add a newline there and every line
         * reported for every fragment shifts by one, silently.  Pinned by
         * t/comcon_pom_origin.t.
         */
        wbuf[0] = '\0';
        stack_v = JS_GetPropertyStr(sctx, exc, "stack");
        if (JS_IsString(stack_v)) {
            st = JS_ToCString(sctx, stack_v);
            if (st != NULL) {
                where = strstr(st, NGX_JS_COMCON_FRAGMENT_ORIGIN ":");
                if (where != NULL) {
                    where += sizeof(NGX_JS_COMCON_FRAGMENT_ORIGIN ":") - 1;
                    for (wn = 0; wn + 1 < (int) sizeof(wbuf); wn++) {
                        if (!((where[wn] >= '0' && where[wn] <= '9')
                              || where[wn] == ':'))
                        {
                            break;
                        }
                        wbuf[wn] = where[wn];
                    }
                    wbuf[wn] = '\0';
                }
                JS_FreeCString(sctx, st);
            }
        }
        JS_FreeValue(sctx, stack_v);

        if (wbuf[0] != '\0') {
            retv = JS_ThrowTypeError(hctx, "comcon: fragment: %s at %s:%s",
                                     s ? s : "error",
                                     NGX_JS_COMCON_FRAGMENT_ORIGIN, wbuf);
        } else {
            retv = JS_ThrowTypeError(hctx, "comcon: fragment: %s",
                                     s ? s : "error");
        }
        if (s != NULL) {
            JS_FreeCString(sctx, s);
        }
        JS_FreeValue(sctx, exc);

    } else if (pending) {
        /*
         * The fragment's promise never settled, and running the compartment's
         * own jobs is the only thing that could have settled it.
         *
         * Reported rather than waited on, because there is nothing to wait for:
         * a compartment reaches no timer and no socket, so an await on anything
         * outside it is an await on something that will never arrive.  Saying so
         * is the useful answer -- the alternative was JSON.stringify on a
         * pending promise, which is "{}", a plausible-looking empty object.
         */
        retv = ngx_js_comcon_refuse(hctx, NGX_JS_REFUSAL_INVOKE_PENDING,
                   "comcon: fragment returned a promise that is still pending "
                   "after its own jobs have run -- a compartment reaches no "
                   "timer and no socket, so nothing outside it can settle an "
                   "await");

    } else {
        /* Materialize the result — INCLUDING any getters in the returned object
           — while still UNDER THE TENANT COMPARTMENT, so a reach attempt hidden
           in a return-value getter (SR-1 HIGH-1) is gated. Leaving the
           compartment before JS_JSONStringify would run those getters as
           HOST_ROOT and bypass the A1 gate. */
        jstr = JS_JSONStringify(sctx, result, JS_UNDEFINED, JS_UNDEFINED);
        JS_FreeValue(sctx, result);

        /*
         * A fragment that returns `undefined` returns UNDEFINED, not a syntax
         * error.
         *
         * JSON.stringify(undefined) is undefined -- not the string "undefined",
         * and not any JSON text -- so this used to hand `undefined` to
         * JS_ParseJSON and the host saw
         *
         *     SyntaxError: unexpected token: 'undefined'   at <result>:1:1
         *
         * which names neither the fragment nor the cause.  And `undefined` is not
         * an exotic return value here: it is what EVERY DENIED GATE produces.  A
         * policy whose last statement reads a redacted field, or calls an
         * operation the mediation refuses, returns it by construction -- so the
         * one path an operator is most likely to hit while tightening a policy
         * was the one that reported an internal parse failure.  Found while
         * testing `window`, whose probe returned a denied call directly; every
         * earlier probe happened to wrap its result in an object or a string.
         *
         * The same applies to a function or a symbol, which JSON also declines to
         * represent: undefined crossing as undefined is the honest answer, since
         * only data crosses and there is no data here.
         */
        if (JS_IsUndefined(jstr)) {
            JS_FreeValue(sctx, jstr);
            retv = JS_UNDEFINED;
        } else {
            s = JS_ToCStringLen(sctx, &len, jstr);
            retv = (s != NULL) ? JS_ParseJSON(hctx, s, len, "<result>")
                               : JS_UNDEFINED;
            if (s != NULL) {
                JS_FreeCString(sctx, s);
            }
            JS_FreeValue(sctx, jstr);
        }
    }

    /*
     * ONE BOUNDARY FOR ALL OF IT, and the fragment's identity, posture and
     * allowance all end where the COMPARTMENT does.
     *
     * These three restores used to sit above the marshalling, which looks
     * harmless until you remember that SR-1 deliberately materializes the result
     * INSIDE the tenant compartment: a getter on the returned object is fragment
     * code, and it runs during JS_JSONStringify.  With the identity already
     * restored, such a getter held capabilities that were no longer "its own" and
     * was refused as cap.owner -- so a fragment's own return value could not read
     * its own grant.  t/comcon_include_sr1.t caught it, because its escape probe
     * distinguishes `null` (the reach gate denying) from `undefined` (something
     * else denying) and suddenly got the wrong one.
     *
     * The same argument applies to the posture and the allowance, so they move
     * with it: work that happens inside the compartment is the fragment's work,
     * and it is gated, metered and charged as such.  A boundary that is in three
     * places is a boundary you have to be reminded of by a test.
     */
    if (mode_pushed) {
        ngx_js_compartment_mode_set(saved_mode);
    }

    ngx_js_compartment_frag_set(saved_frag);

    /* the allowance was for THAT call only */
    JS_SetMemoryLimit(jcf->comcon_rt, 64 * 1024 * 1024);

    ngx_js_compartment_leave(prev);

    if (metered) {
        w->request_deadline_ms = old_deadline;
    }
    JS_FreeValue(sctx, arg);

    return retv;
}


/*
 * COMCON increment D4a — free a held confined fragment (rebuild-on-write). When
 * `replace` supersedes an epoch that falls out of the bounded rollback window,
 * its fragment is released so live rewrite does not accumulate compiled
 * fragments. The slot is set to JS_UNDEFINED (invoking a freed handle then
 * errors — see __invokeConfined). Idempotent; out-of-range is a no-op.
 */
JSValue
ngx_js_comcon_free_confined(JSContext *hctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_js_conf_t  *jcf;
    JSContext      *sctx;
    JSValue        *frags;
    int64_t         handle = 0;

    jcf = ngx_js_comcon_jcf;
    if (jcf == NULL || jcf->comcon_ctx == NULL || jcf->comcon_frags == NULL) {
        return JS_UNDEFINED;
    }
    sctx = jcf->comcon_ctx;

    if (argc < 1) {
        return JS_UNDEFINED;
    }
    JS_ToInt64(hctx, &handle, argv[0]);

    if (handle < 0 || (ngx_uint_t) handle >= jcf->comcon_frags->nelts) {
        return JS_UNDEFINED;
    }

    frags = (JSValue *) jcf->comcon_frags->elts;
    if (!JS_IsUndefined(frags[handle])) {
        JS_FreeValue(sctx, frags[handle]);
        frags[handle] = JS_UNDEFINED;
    }

    return JS_UNDEFINED;
}


/*
 * COMCON increment D0 — POM substrate (diagnostic bridge). Reflects a compiled
 * fragment (argv[0], a host bytecode function) as a POM node tree at module/
 * function granularity (POM.md granularity floor). Read-only, no fragment code
 * runs. The lazy NodeView JS surface (text/quote/describe/query) lands in D1;
 * this is the thin C bridge that lets the D0 gate assert kinds + span stability.
 */
JSValue
ngx_js_comcon_pom_inspect(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    if (argc < 1) {
        return JS_UNDEFINED;
    }

    return js_comcon_pom_inspect(ctx, argv[0]);
}


/*
 * COMCON increment D1 — backs the lazy NodeView. argv[0] is the root fragment,
 * argv[1] a path (array of small non-negative ints). Reflects the single node
 * at that path; the JS layer (comcon.pom) wraps it and returns reads as quote()
 * values. Path capped at NGX_JS_POM_MAX_DEPTH (module/function nesting is small).
 */
#define NGX_JS_POM_MAX_DEPTH  64

JSValue
ngx_js_comcon_pom_node_at(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    int       path[NGX_JS_POM_MAX_DEPTH];
    int       pathlen = 0;
    uint32_t  len, i;
    JSValue   lenv;

    if (argc < 2) {
        return JS_UNDEFINED;
    }

    /* argv[1] must be an array; read its length then each int element. */
    lenv = JS_GetPropertyStr(ctx, argv[1], "length");
    if (JS_IsException(lenv)) {
        return JS_EXCEPTION;
    }
    if (JS_ToUint32(ctx, &len, lenv) < 0) {
        JS_FreeValue(ctx, lenv);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, lenv);

    if (len > NGX_JS_POM_MAX_DEPTH) {
        return JS_UNDEFINED;
    }

    for (i = 0; i < len; i++) {
        JSValue  ev = JS_GetPropertyUint32(ctx, argv[1], i);
        int32_t  idx;

        if (JS_IsException(ev)) {
            return JS_EXCEPTION;
        }
        if (JS_ToInt32(ctx, &idx, ev) < 0) {
            JS_FreeValue(ctx, ev);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, ev);

        if (idx < 0) {
            return JS_UNDEFINED;
        }
        path[pathlen++] = idx;
    }

    return js_comcon_pom_node_at(ctx, argv[0], path, pathlen);
}


/*
 * COMCON increment D5a — references/call-sites of a name in a fragment subtree.
 * argv[0] = fragment, argv[1] = target name. Backs node.callsites()/references().
 */
/*
 * comcon.__parse(source) -> ESTree  [D5b-2, internal]
 *
 * The vendored acorn (src/js/vendor/, MIT, pinned) parsed into an ESTree with
 * byte RANGES. Ranges are the load-bearing output: D5b-3 rewrites by splicing
 * at offsets into the ORIGINAL source and never re-prints the AST, so no code
 * generator enters the TCB and comments/formatting round-trip exactly.
 *
 * LAZY. acorn is ~238 KB of JS; evaluating it in every worker at startup would
 * tax every config, and the overwhelming majority never harden anything. It is
 * evaluated on first use and cached on the comcon object. Loaded pre-fork in
 * the master when a config does use it, so workers inherit it by COW.
 *
 * FAILS CLOSED, which is the whole reason a proven parser was vendored rather
 * than hand-rolled: a SyntaxError propagates as a thrown exception, so a caller
 * that cannot parse a source cannot proceed to admit it.
 *
 * HOST-SIDE ONLY. This is trusted analysis over untrusted TEXT: it is not
 * reachable from a confined fragment, and it never evaluates what it parses.
 */
JSValue
ngx_js_comcon_parse(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    JSValue      global, comcon, acorn, parse, ret, opts, arg;
    const char  *src;
    size_t       len;

    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "comcon.__parse(source[, expr]): string required");
    }

    global = JS_GetGlobalObject(ctx);
    comcon = JS_GetPropertyStr(ctx, global, "comcon");
    if (JS_IsException(comcon)) {
        JS_FreeValue(ctx, global);
        return comcon;
    }

    acorn = JS_GetPropertyStr(ctx, comcon, "__acorn");
    if (JS_IsUndefined(acorn)) {
        /* First use: evaluate the vendored parser into this context. */
        JSValue r = JS_Eval(ctx, ngx_js_vendor_acorn_js,
                            sizeof(ngx_js_vendor_acorn_js) - 1,
                            "<vendor/acorn.js>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) {
            JS_FreeValue(ctx, acorn);
            JS_FreeValue(ctx, comcon);
            JS_FreeValue(ctx, global);
            return r;
        }
        JS_FreeValue(ctx, r);

        JS_FreeValue(ctx, acorn);
        acorn = JS_GetPropertyStr(ctx, global, "acorn");
        if (!JS_IsObject(acorn)) {
            JS_FreeValue(ctx, acorn);
            JS_FreeValue(ctx, comcon);
            JS_FreeValue(ctx, global);
            return JS_ThrowInternalError(ctx,
                       "comcon.__parse: vendored parser did not define acorn");
        }
        /* Cache on comcon, and take the global binding away again: the parser
           is an internal of the analysis path, not part of the host surface. */
        JS_SetPropertyStr(ctx, comcon, "__acorn", JS_DupValue(ctx, acorn));
        JS_DeleteProperty(ctx, global, JS_NewAtom(ctx, "acorn"), 0);
    }

    /*
     * A COMCON fragment's source is `function(req){...}` -- a function
     * EXPRESSION, which is not a valid Program (an anonymous function
     * declaration is a syntax error at statement position). So the caller may
     * ask for expression mode, which uses acorn.parseExpressionAt at offset 0.
     *
     * parseExpressionAt, NOT a paren wrapper: wrapping would shift every range
     * by one, and ranges are the load-bearing output here -- D5b-3 splices at
     * them. Parsing the original string keeps every offset usable as-is.
     */
    parse = JS_GetPropertyStr(ctx, acorn,
                              (argc > 1 && JS_ToBool(ctx, argv[1]))
                                  ? "parseExpressionAt" : "parse");
    if (!JS_IsFunction(ctx, parse)) {
        JS_FreeValue(ctx, parse);
        JS_FreeValue(ctx, acorn);
        JS_FreeValue(ctx, comcon);
        JS_FreeValue(ctx, global);
        return JS_ThrowInternalError(ctx, "comcon.__parse: acorn.parse missing");
    }

    src = JS_ToCStringLen(ctx, &len, argv[0]);
    if (src == NULL) {
        JS_FreeValue(ctx, parse);
        JS_FreeValue(ctx, acorn);
        JS_FreeValue(ctx, comcon);
        JS_FreeValue(ctx, global);
        return JS_EXCEPTION;
    }

    opts = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, opts, "ecmaVersion", JS_NewInt32(ctx, 2022));
    JS_SetPropertyStr(ctx, opts, "ranges",      JS_TRUE);
    JS_SetPropertyStr(ctx, opts, "locations",   JS_TRUE);

    {
        JSValueConst a[3];
        int          n = 2;

        arg  = JS_NewStringLen(ctx, src, len);
        a[0] = arg;
        if (argc > 1 && JS_ToBool(ctx, argv[1])) {
            a[1] = JS_NewInt32(ctx, 0);   /* parseExpressionAt(src, 0, opts) */
            a[2] = opts;
            n = 3;
        } else {
            a[1] = opts;
        }
        /* A SyntaxError from here propagates to the caller unchanged: FAIL CLOSED. */
        ret = JS_Call(ctx, parse, acorn, n, a);
        JS_FreeValue(ctx, arg);
    }

    JS_FreeCString(ctx, src);
    JS_FreeValue(ctx, opts);
    JS_FreeValue(ctx, parse);
    JS_FreeValue(ctx, acorn);
    JS_FreeValue(ctx, comcon);
    JS_FreeValue(ctx, global);
    return ret;
}


JSValue
ngx_js_comcon_pom_callsites(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    const char  *name;
    JSValue      r;

    if (argc < 2) {
        return JS_UNDEFINED;
    }

    name = JS_ToCString(ctx, argv[1]);
    if (name == NULL) {
        return JS_EXCEPTION;
    }

    r = js_comcon_pom_callsites(ctx, argv[0], name);
    JS_FreeCString(ctx, name);

    return r;
}


/*
 * COMCON step-4 (directive retirement): host-JS operators that configure the
 * tenant compartment from the single js_source root script, so the `js_tenant_*`
 * nginx.conf directives can be retired (the fundament: never add directives).
 * They run during host eval (init_conf), BEFORE ngx_js_eval_tenant_sources and
 * (after the reorder) before ngx_js_compartment_policy_init, populating the same
 * jcf fields the directives set — so behavior is identical.
 */

/* comcon.mode("enforce"|"audit"|"learn") — retires js_tenant_mode. */
JSValue
ngx_js_comcon_op_mode(JSContext *ctx, JSValueConst this_val, int argc,
    JSValueConst *argv)
{
    ngx_js_conf_t  *jcf = ngx_js_comcon_jcf;
    const char     *m;

    if (jcf == NULL) {
        return JS_ThrowInternalError(ctx, "comcon.mode: no conf");
    }

    m = JS_ToCString(ctx, argv[0]);
    if (m == NULL) {
        return JS_EXCEPTION;
    }

    if (ngx_strcmp(m, "enforce") == 0) {
        jcf->tenant_mode = NGX_JS_TENANT_ENFORCE;
    } else if (ngx_strcmp(m, "audit") == 0) {
        jcf->tenant_mode = NGX_JS_TENANT_AUDIT;
    } else if (ngx_strcmp(m, "learn") == 0) {
        jcf->tenant_mode = NGX_JS_TENANT_LEARN;
    } else {
        JS_FreeCString(ctx, m);
        return JS_ThrowTypeError(ctx,
            "comcon.mode: expected \"enforce\", \"audit\" or \"learn\"");
    }

    JS_FreeCString(ctx, m);

    /*
     * Write BOTH: jcf->tenant_mode so a config-time call survives
     * policy_init() (which reads it at the end of the host eval), and the
     * effective mode so a REQUEST-time call is not silently inert -- the
     * audit-first rollout is a live session's verb, and it used to report
     * success while the compartment kept the mode it had.  Per process; there is
     * no fleet-wide fan-out (std.ops says so rather than implying otherwise).
     */
    ngx_js_compartment_mode_set((ngx_js_tenant_mode_e) jcf->tenant_mode);

    /* Return the effective name, so a caller can VERIFY the switch landed
       instead of trusting that it did. */
    return JS_NewString(ctx, ngx_js_tenant_mode_name());
}





/*
 * ngx_js_init_conf — called after ngx_conf_parse() completes.
 *
 * At this point every server{}, location{}, upstream{} in nginx.conf
 * has been parsed and its C config structs are fully populated.  We
 * create the master QuickJS runtime, install the COM, and evaluate
 * every js_source file.  Any mutations JS makes to COM objects
 * (Phases 2+) write directly into cycle-pool memory and are visible
 * to worker processes after fork().
 */
static char *
ngx_js_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_js_conf_t  *jcf = conf;
    ngx_uint_t      i;
    ngx_str_t      *path;
    u_char         *src;
    size_t          src_len;

    ngx_js_comcon_jcf = jcf;    /* M-CFG: reachable from comcon.include CFunctions */

    if (jcf->sources.nelts == 0 && jcf->tenant_sources.nelts == 0) {
        return NGX_CONF_OK;    /* nothing to do — pure static config */
    }

    /* ---- Create the master QuickJS runtime ---- */

    jcf->rt = JS_NewRuntime();
    if (jcf->rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: JS_NewRuntime() failed");
        return NGX_CONF_ERROR;
    }

    js_std_init_handlers(jcf->rt);
    JS_SetModuleLoaderFunc2(jcf->rt, NULL,
                            js_module_loader, js_module_check_attributes,
                            NULL);
    JS_SetSharedArrayBufferFunctions(jcf->rt, &ngx_js_sab_funcs);

    /* Limit memory to 64 MB for the config-phase runtime */
    JS_SetMemoryLimit(jcf->rt, 64 * 1024 * 1024);

    jcf->ctx = JS_NewContext(jcf->rt);
    if (jcf->ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: JS_NewContext() failed");
        goto failed_rt;
    }

    /* ---- Register std and os built-in modules ---- */

    if (js_init_module_std(jcf->ctx, "std") == NULL
        || js_init_module_os(jcf->ctx, "os") == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: failed to register std/os modules");
        goto failed_ctx;
    }

    /* ---- Install nginx.* COM namespace ---- */

    if (ngx_js_com_init(jcf->ctx, cycle) != NGX_OK) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: COM initialisation failed");
        goto failed_ctx;
    }

    /* ---- Initialise master lifecycle event handler registry ---- */

    jcf->master_handlers = JS_NewObject(jcf->ctx);
    if (JS_IsException(jcf->master_handlers)) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "js: failed to create master_handlers object");
        goto failed_ctx;
    }

    /* ---- Evaluate each js_source file in declaration order ---- */

    path = jcf->sources.elts;

    for (i = 0; i < jcf->sources.nelts; i++) {

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "js: executing \"%V\"", &path[i]);

        src = ngx_js_read_file(cycle, &path[i], &src_len);
        if (src == NULL) {
            goto failed_ctx;
        }

        if (ngx_js_eval_module(jcf->ctx, jcf->rt,
                               src, src_len, path[i].data,
                               cycle->log)
            != NGX_CONF_OK)
        {
            goto failed_ctx;
        }
    }

    /* COMCON A4: fresh denial counters + this cycle's audit/enforce mode.
     * Runs AFTER the host eval so comcon.mode() in the root script takes effect,
     * but BEFORE any confined include fragment is invoked (the only thing that
     * gates). The host eval is HOST_ROOT and produces no gate events. Workers
     * inherit the post-init state by fork. */
    ngx_js_compartment_policy_init((ngx_js_tenant_mode_e) jcf->tenant_mode);

    /*
     * The master runtime stays alive until exit_master().  Worker
     * processes inherit it via fork() (copy-on-write) and use their
     * private copies; init_process() just wires w->rt / w->ctx to it.
     *
     * Start the SharedWorker manager thread so that workers can call
     * new SharedWorker(url) from request handlers.
     */
    if (ngx_js_sw_manager_start(jcf, cycle) != NGX_OK) {
        goto failed_ctx;
    }

    /*
     * Phase 2/3 messaging uses the existing nginx channel socketpairs
     * (ngx_processes[i].channel[0/1]) created by ngx_spawn_process().
     * No extra fds needed here — just set the function pointer hooks in
     * ngx_js_init_module() once the processes are spawned.
     */

    /*
     * P17: all JS scripts have now been evaluated.  Any server.on('accept')
     * or server.addL4Filter() calls they made have been recorded in the
     * per-server ngx_js_http_srv_conf_t.  Override ls->handler on every
     * standard HTTP listen socket whose default server has JS hooks so that
     * ngx_js_srv_accept_handler fires instead of ngx_http_init_connection.
     */
    ngx_js_srv_install_accept_hooks(cycle);

    return NGX_CONF_OK;

failed_ctx:
    ngx_js_tenant_teardown(jcf);          /* COMCON A3 */

    /*
     * Pre-existing bug fixed alongside A3: on a failed eval, the GC-tracked
     * master_handlers JSValue held in jcf must be freed BEFORE JS_FreeContext
     * (same rule the exit paths follow), or JS_FreeRuntime below trips the
     * "list_empty(&rt->gc_obj_list)" assertion — e.g. on a SIGHUP reload
     * whose js_source throws (seen: createSocket EADDRINUSE), aborting the
     * MASTER instead of rolling back to the old cycle.
     */
    if (!JS_IsUninitialized(jcf->master_handlers)) {
        JS_FreeValue(jcf->ctx, jcf->master_handlers);
        jcf->master_handlers = JS_UNINITIALIZED;
    }

    JS_FreeContext(jcf->ctx);
    jcf->ctx = NULL;

    if (jcf->comcon_ctx != NULL) {   /* M-CFG: free frags, context, then its runtime */
        if (jcf->comcon_frags != NULL) {
            ngx_uint_t  fi;
            JSValue    *fv = jcf->comcon_frags->elts;
            for (fi = 0; fi < jcf->comcon_frags->nelts; fi++) {
                JS_FreeValue(jcf->comcon_ctx, fv[fi]);
            }
            jcf->comcon_frags->nelts = 0;
        }
        if (jcf->comcon_frags_pool != NULL) {
            ngx_destroy_pool(jcf->comcon_frags_pool);
            jcf->comcon_frags_pool = NULL;
            jcf->comcon_frags = NULL;
        }
        JS_FreeContext(jcf->comcon_ctx);
        jcf->comcon_ctx = NULL;
    }
    if (jcf->comcon_rt != NULL) {
        JS_FreeRuntime(jcf->comcon_rt);
        jcf->comcon_rt = NULL;
    }

failed_rt:
    js_std_free_handlers(jcf->rt);
    JS_FreeRuntime(jcf->rt);
    jcf->rt = NULL;

    return NGX_CONF_ERROR;
}


/*
 * ngx_js_interrupt_handler — polled by QuickJS every ~100 bytecodes.
 *
 * Returns 1 to abort JS execution when the per-request deadline has
 * passed.  The deadline (CLOCK_MONOTONIC milliseconds) is set in
 * ngx_js_content_handler() before JS_Call and cleared afterwards.
 * clock_gettime() is async-signal-safe and does not require the nginx
 * event loop to be running, so it fires even inside a tight JS loop.
 */
static int
ngx_js_interrupt_handler(JSRuntime *rt, void *opaque)
{
    ngx_js_worker_t  *w = opaque;
    struct timespec   ts;
    uint64_t          now_ms;

    if (w->request_deadline_ms == 0) {
        return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now_ms = (uint64_t) ts.tv_sec * 1000 + (uint64_t) ts.tv_nsec / 1000000;

    return now_ms >= w->request_deadline_ms ? 1 : 0;
}


/* ------------------------------------------------------------------ */
/* F4 — bcast fd event handler                                         */
/* ------------------------------------------------------------------ */

/*
 * Context stored in conn->data for the bcast fd nginx connection.
 * Allocated in cycle->pool so no explicit free is needed.
 */
typedef struct {
    ngx_js_worker_t  *w;
} ngx_js_bcast_ctx_t;


/*
 * Called by nginx event loop when bcast_fds[ngx_worker][1] becomes readable.
 * Drains all pending broadcast messages, creates per-worker socket state for
 * each received socket fd, and calls nginx.onSocket(sock) if registered.
 */
static void
ngx_js_bcast_recv_handler(ngx_event_t *ev)
{
    ngx_connection_t       *conn;
    ngx_js_bcast_ctx_t     *bctx;
    ngx_js_worker_t        *w;
    JSContext              *ctx, *job_ctx;
    uint8_t                 recv_body[NGX_JS_BCAST_MAX + 1];
    char                    cmsg_buf[CMSG_SPACE(sizeof(int))];
    struct iovec            iov;
    struct msghdr           mh;
    ssize_t                 n;
    uint32_t                handle, addr_len;
    char                   *addr_ptr;
    int                     recv_fd;
    struct cmsghdr         *cmh;
    ngx_js_socket_state_t  *st;
    ngx_uint_t              i;
    const char             *colon;
    long                    port;
    JSValue                 global, nginx_obj, on_sock, sock_val, ret;

    conn = ev->data;
    bctx = conn->data;
    w    = bctx->w;
    ctx  = w->ctx;

    /*
     * Peer-close (write end of the socketpair closed, e.g. during shutdown):
     * ev->eof is set by the epoll layer.  Remove the read event and free the
     * connection slot so epoll stops re-firing the handler in a tight loop.
     * We do NOT close the fd here — bcast_fd ownership stays with the worker.
     */
    if (ev->eof) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, ngx_cycle->log, 0,
                       "js bcast: channel EOF, deactivating");
        ngx_del_event(ev, NGX_READ_EVENT, 0);
        ngx_free_connection(conn);
        conn->fd    = (ngx_socket_t) -1;
        w->bcast_conn = NULL;
        ngx_free(bctx);
        return;
    }

    for ( ;; ) {
        iov.iov_base = recv_body;
        iov.iov_len  = sizeof(recv_body) - 1;  /* leave room for NUL */

        ngx_memzero(&mh, sizeof(mh));
        mh.msg_iov        = &iov;
        mh.msg_iovlen     = 1;
        mh.msg_control    = cmsg_buf;
        mh.msg_controllen = sizeof(cmsg_buf);

        n = recvmsg(conn->fd, &mh, MSG_DONTWAIT);
        if (n <= 0) {
            if (n == 0) {
                /* EOF without ev->eof flag: same cleanup */
                ngx_del_event(ev, NGX_READ_EVENT, 0);
                ngx_free_connection(conn);
                conn->fd    = (ngx_socket_t) -1;
                w->bcast_conn = NULL;
                ngx_free(bctx);
            }
            break;
        }

        if (n < 1 || (mh.msg_flags & MSG_TRUNC)) {
            continue;
        }

        /* Dispatch on the type byte (Phase 2 bcast format) */

        if (recv_body[0] == NGX_JS_BCAST_TYPE_SUSPEND) {
            (void) ngx_js_disable_accept_events((ngx_cycle_t *) ngx_cycle);
            (void) send(conn->fd, "\x01", 1, 0);  /* ack to manager */
            continue;
        }

        if (recv_body[0] == NGX_JS_BCAST_TYPE_RESUME) {
            (void) ngx_enable_accept_events((ngx_cycle_t *) ngx_cycle);
            (void) send(conn->fd, "\x01", 1, 0);  /* ack to manager */
            continue;
        }

        /* NGX_JS_BCAST_TYPE_SOCKET: socket delivery */

        if ((size_t) n < NGX_JS_BCAST_HDR) {
            continue;
        }

        /*
         * The header is [type:u8][handle:u32][addr_len:u32], so both 32-bit
         * fields sit at ODD offsets in the receive buffer.  Casting to
         * uint32_t* and dereferencing is a misaligned load -- undefined
         * behaviour, flagged by UBSAN, and on a strict-alignment target a fault
         * or a silently wrong read.  The `(void *)` in the old cast is exactly
         * what stopped -Wcast-align from saying so.  Copy the bytes out, which
         * is what the SENDER already does when it packs them (ngx_js_sw.c).
         */
        ngx_memcpy(&handle, recv_body + 1, sizeof(uint32_t));
        ngx_memcpy(&addr_len, recv_body + 1 + sizeof(uint32_t),
                   sizeof(uint32_t));

        if (addr_len == 0 || addr_len > 63
            || (size_t) n < NGX_JS_BCAST_HDR + addr_len)
        {
            continue;
        }

        addr_ptr           = (char *) recv_body + NGX_JS_BCAST_HDR;
        addr_ptr[addr_len] = '\0';

        /* Extract socket fd from SCM_RIGHTS */
        recv_fd = -1;
        cmh = CMSG_FIRSTHDR(&mh);
        if (cmh != NULL
            && cmh->cmsg_level == SOL_SOCKET
            && cmh->cmsg_type  == SCM_RIGHTS
            && cmh->cmsg_len   == CMSG_LEN(sizeof(int)))
        {
            ngx_memcpy(&recv_fd, CMSG_DATA(cmh), sizeof(int));
        }

        if (recv_fd < 0) {
            continue;
        }

        if (handle >= NGX_JS_SOCKET_REG_MAX) {
            close(recv_fd);
            continue;
        }

        if (ngx_js_socket_reg[handle] != NULL) {
            /* Slot already occupied — log warning and discard */
            ngx_log_error(NGX_LOG_WARN, ngx_cycle->log, 0,
                          "js: bcast: handle %uD already occupied,"
                          " closing received fd", handle);
            close(recv_fd);
            continue;
        }

        /* Parse port from "host:port" */
        colon = strrchr(addr_ptr, ':');
        port  = (colon != NULL) ? strtol(colon + 1, NULL, 10) : 0;

        st = ngx_alloc(sizeof(ngx_js_socket_state_t), ngx_cycle->log);
        if (st == NULL) {
            close(recv_fd);
            continue;
        }

        /*
         * ngx_alloc() is malloc(): every field has to be written, and `owner`
         * was not.  The ownership gate on close()/broadcast() reads it, so a
         * socket that arrived over SCM_RIGHTS was gated on whatever happened to
         * be in that heap word.  A broadcast socket belongs to the host of the
         * worker receiving it.
         */
        ngx_memzero(st, sizeof(ngx_js_socket_state_t));

        st->fd          = recv_fd;
        st->port        = (uint16_t) port;
        st->in_listening = 0;
        st->owner        = NGX_JS_COMPARTMENT_HOST_ROOT;
        ngx_cpystrn((u_char *) st->addr, (u_char *) addr_ptr,
                    sizeof(st->addr));

        /* Installs AND bumps the slot generation, retiring any handle still
         * held for the socket that used to live here. */
        ngx_js_socket_reg_install(handle, st);

        /* Register in worker-local registry for cleanup */
        for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
            if (w->local_socket_reg[i] == NULL) {
                w->local_socket_reg[i] = st;
                break;
            }
        }

        /* Call nginx.onSocket(sock) if registered */
        global    = JS_GetGlobalObject(ctx);
        nginx_obj = JS_GetPropertyStr(ctx, global, "nginx");
        JS_FreeValue(ctx, global);

        if (!JS_IsException(nginx_obj) && !JS_IsUndefined(nginx_obj)) {
            on_sock = JS_GetPropertyStr(ctx, nginx_obj, "onSocket");
            JS_FreeValue(ctx, nginx_obj);

            if (JS_IsFunction(ctx, on_sock)) {
                sock_val = ngx_js_socket_wrap(ctx, handle);
                if (!JS_IsException(sock_val)) {
                    ret = JS_Call(ctx, on_sock, JS_UNDEFINED, 1, &sock_val);
                    JS_FreeValue(ctx, sock_val);
                    if (JS_IsException(ret)) {
                        ngx_js_log_exception(ctx, ngx_cycle->log);
                    }
                    JS_FreeValue(ctx, ret);
                }
            }

            JS_FreeValue(ctx, on_sock);

        } else {
            JS_FreeValue(ctx, nginx_obj);
        }
    }

    while (JS_ExecutePendingJob(w->rt, &job_ctx) > 0) { }

    ngx_js_async_check(w);
    ngx_js_bf_async_check(w);
    ngx_js_sf_async_check(w);
}


/*
 * ngx_js_bcast_ensure_active — register the per-worker bcast event handler
 * in the nginx epoll/kqueue event loop.  Idempotent; no-op if already done
 * or if no bcast fd is available.
 *
 * Must only be called from inside the worker's event loop (e.g., from a
 * request content handler), after ngx_event_process_init() has run and
 * ngx_cycle->free_connections is non-NULL.
 */
void
ngx_js_bcast_ensure_active(ngx_js_worker_t *w)
{
    ngx_connection_t    *bcast_conn;
    ngx_js_bcast_ctx_t  *bcast_ctx;

    if (w == NULL || w->bcast_conn != NULL || w->bcast_fd < 0) {
        return;   /* already active, no fd, or no worker */
    }

    bcast_ctx = ngx_alloc(sizeof(ngx_js_bcast_ctx_t), ngx_cycle->log);
    if (bcast_ctx == NULL) {
        return;
    }

    bcast_ctx->w = w;

    bcast_conn = ngx_get_connection(w->bcast_fd, ngx_cycle->log);
    if (bcast_conn == NULL) {
        ngx_free(bcast_ctx);
        return;
    }

    bcast_conn->data          = bcast_ctx;
    bcast_conn->read->handler = ngx_js_bcast_recv_handler;
    bcast_conn->read->log     = ngx_cycle->log;

    if (ngx_add_event(bcast_conn->read, NGX_READ_EVENT, 0) != NGX_OK) {
        ngx_free_connection(bcast_conn);
        bcast_conn->fd = (ngx_socket_t) -1;
        ngx_free(bcast_ctx);
        return;
    }

    w->bcast_conn = bcast_conn;
}


/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Phase 2/3 — JS messaging over existing nginx channel socketpairs    */
/* ------------------------------------------------------------------ */

/*
 * ngx_js_dispatch_msg — shared helper: deserialise payload and call all
 * handlers registered under `event` in jcf->master_handlers.
 * `extra_argc` / `extra_argv` are prepended before the data argument
 * (used for 'workerMessage' which passes slot as first arg).
 */
static void
ngx_js_dispatch_msg(JSContext *ctx, JSRuntime *rt, ngx_js_conf_t *jcf,
    const char *event, const uint8_t *buf, size_t payload_len,
    int extra_argc, JSValue *extra_argv)
{
    JSValue      msg, arr, len_val, fn, ret;
    uint32_t     i, len;
    JSValue      argv[4];
    int          argc;

    if (JS_IsUninitialized(jcf->master_handlers)) {
        return;
    }

    arr = JS_GetPropertyStr(ctx, jcf->master_handlers, event);
    if (!JS_IsArray(ctx, arr)) {
        JS_FreeValue(ctx, arr);
        return;
    }

    msg = JS_ReadObject(ctx, buf, payload_len, JS_READ_OBJ_REFERENCE);
    if (JS_IsException(msg)) {
        JSValue      exc;
        const char  *str;

        exc = JS_GetException(ctx);
        str = JS_ToCString(ctx, exc);
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "js: channel msg: deserialize error: %s",
                      str ? str : "(null)");
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, arr);
        return;
    }

    /* Build call args: [extra_argv..., msg] */
    argc = 0;
    for (i = 0; i < (uint32_t) extra_argc; i++) {
        argv[argc++] = extra_argv[i];
    }
    argv[argc++] = msg;

    len_val = JS_GetPropertyStr(ctx, arr, "length");
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    for (i = 0; i < len; i++) {
        fn = JS_GetPropertyUint32(ctx, arr, i);
        if (JS_IsFunction(ctx, fn)) {
            ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
            if (JS_IsException(ret)) {
                JSValue      exc;
                const char  *str;

                exc = JS_GetException(ctx);
                str = JS_ToCString(ctx, exc);
                ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                              "js: nginx.on('%s') exception: %s",
                              event, str ? str : "(null)");
                JS_FreeCString(ctx, str);
                JS_FreeValue(ctx, exc);
            }
            JS_FreeValue(ctx, ret);
        }
        JS_FreeValue(ctx, fn);
    }

    JS_FreeValue(ctx, msg);
    JS_FreeValue(ctx, arr);

    /* drain microtasks */
    {
        JSContext  *pctx;
        while (JS_ExecutePendingJob(rt, &pctx) > 0) { /* nothing */ }
    }
}


/*
 * Phase 2: called by ngx_channel_handler (via function pointer hook) when
 * the worker receives NGX_CMD_JS_MESSAGE on its channel fd.
 * fd        — the channel fd (ngx_channel, = channel[1], O_NONBLOCK)
 * payload_len — bytes of JS-serialized data that follow in the stream
 */
static void
ngx_js_handle_worker_channel_msg(ngx_socket_t fd, ngx_int_t payload_len)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;
    static uint8_t    buf[NGX_JS_MSG_MAX];
    ssize_t           n;
    ngx_int_t         total;

    if (payload_len <= 0 || (size_t) payload_len > NGX_JS_MSG_MAX) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "js: worker channel msg: invalid payload_len %i",
                      payload_len);
        return;
    }

    /* Read payload — loop to handle partial reads on O_NONBLOCK stream */
    total = 0;
    while (total < payload_len) {
        n = recv(fd, buf + total, (size_t) (payload_len - total), 0);
        if (n > 0) {
            total += (ngx_int_t) n;
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                          "js: worker channel msg: recv() failed");
            return;
        }
        /* EAGAIN: yield briefly — payload should be in buffer already */
    }

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return;
    }

    w = jcf->worker;
    if (w == NULL) {
        return;
    }

    ngx_js_dispatch_msg(w->ctx, w->rt, jcf, "message",
                        buf, (size_t) total, 0, NULL);
}


/*
 * P16: called in this worker when master sends NGX_CMD_JS_LOAD_PLUGIN.
 * Payload format: "absolute_dir\0config_json\0".
 * Reads the payload, then calls ngx_js_load_plugin to evaluate the plugin
 * in this worker's runtime.
 */
static void
ngx_js_handle_worker_load_plugin(ngx_socket_t fd, ngx_int_t payload_len)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;
    static uint8_t    buf[NGX_JS_MSG_MAX];
    ssize_t           n;
    ngx_int_t         total;
    const char       *dir, *config_json;
    size_t            dir_len;

    if (payload_len <= 0 || (size_t) payload_len > NGX_JS_MSG_MAX) {
        ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0,
                      "js: load_plugin: invalid payload_len %i", payload_len);
        return;
    }

    total = 0;
    while (total < payload_len) {
        n = recv(fd, buf + total, (size_t) (payload_len - total), 0);
        if (n > 0) {
            total += (ngx_int_t) n;
            continue;
        }
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, ngx_errno,
                          "js: load_plugin: recv() failed");
            return;
        }
    }

    /* Ensure NUL-termination */
    buf[total] = '\0';

    jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL) {
        return;
    }

    w = jcf->worker;
    if (w == NULL || w->ctx == NULL) {
        return;
    }

    /* Parse two NUL-terminated strings from payload */
    dir        = (const char *) buf;
    dir_len    = strnlen(dir, (size_t) total);
    config_json = (dir_len + 1 < (size_t) total)
                  ? (const char *) buf + dir_len + 1
                  : NULL;

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "js: P16: worker loading plugin from broadcast: %s", dir);

    (void) ngx_js_load_plugin(w->ctx, w->rt, (ngx_cycle_t *) ngx_cycle,
                              dir, config_json);
}


/*
 * Phase 3: called by the master loop after SIGIO to drain worker→master
 * JS messages from all active channel[0] fds.
 * Performs non-blocking reads; ignores EAGAIN/unknown commands.
 */
static void
ngx_js_handle_master_channel_msgs(ngx_cycle_t *cycle)
{
    ngx_js_conf_t   *jcf;
    ngx_channel_t    ch;
    static uint8_t   buf[NGX_JS_MSG_MAX];
    ngx_int_t        i, payload_len, total;
    ssize_t          n;
    int              fd;
    JSValue          slot_val;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL
        || JS_IsUninitialized(jcf->master_handlers))
    {
        return;
    }

    for (i = 0; i < ngx_last_process; i++) {
        fd = ngx_processes[i].channel[0];
        if (fd < 0) {
            continue;
        }

        /* Non-blocking read of ngx_channel_t header */
        n = recv(fd, &ch, sizeof(ch), MSG_DONTWAIT);
        if (n <= 0) {
            continue;
        }

        if ((size_t) n < sizeof(ch)) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: master channel: short header read from slot %i", i);
            continue;
        }

        if (ch.command != NGX_CMD_JS_WORKER_MSG
            && ch.command != NGX_CMD_JS_USE_PLUGIN)
        {
            /* Not a JS message — log and skip (shouldn't happen) */
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: master channel: unexpected command %ui from slot %i",
                          ch.command, i);
            continue;
        }

        payload_len = (ngx_int_t) ch.fd;   /* repurposed field */
        if (payload_len <= 0 || (size_t) payload_len > NGX_JS_MSG_MAX) {
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "js: master channel: bad payload_len %i from slot %i",
                          payload_len, i);
            continue;
        }

        /* Read payload — use blocking recv since we're in sigsuspend loop */
        total = 0;
        while (total < payload_len) {
            n = recv(fd, buf + total, (size_t) (payload_len - total), 0);
            if (n > 0) {
                total += (ngx_int_t) n;
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
                ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                              "js: master channel: recv() failed, slot %i", i);
                break;
            }
        }

        if (total < payload_len) {
            continue;
        }

        /*
         * P16: NGX_CMD_JS_USE_PLUGIN — worker requests that all OTHER
         * workers load the same plugin.  Broadcast via NGX_CMD_JS_LOAD_PLUGIN
         * to every worker except the one that sent the request (slot i).
         */
        if (ch.command == NGX_CMD_JS_USE_PLUGIN) {
            ngx_int_t  k;

            buf[total] = '\0';  /* safety NUL */
            ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                           "js: P16: master broadcasting plugin load from"
                           " worker %i: %s", i, buf);

            for (k = 0; k < ngx_last_process; k++) {
                if (k == i) {
                    continue;  /* skip the sender */
                }
                if (ngx_processes[k].channel[0] >= 0) {
                    ngx_js_channel_send(ngx_processes[k].channel[0],
                                       NGX_CMD_JS_LOAD_PLUGIN,
                                       (ngx_uint_t) k,
                                       buf, (size_t) total,
                                       cycle->log);
                }
            }
            continue;
        }

        /* Dispatch to nginx.on('workerMessage', fn(slot, data)) */
        slot_val = JS_NewInt32(jcf->ctx, (int32_t) i);
        ngx_js_dispatch_msg(jcf->ctx, jcf->rt, jcf, "workerMessage",
                            buf, (size_t) total, 1, &slot_val);
        JS_FreeValue(jcf->ctx, slot_val);
    }
}


/*
 * ngx_js_init_process — called in each worker after fork().
 *
 * After fork() every worker has a private copy-on-write image of the
 * master's address space, including its JSRuntime and JSContext.  Those
 * objects already contain the fully-evaluated JS environment (all
 * js_source scripts executed, COM installed, handler functions
 * registered in the global scope, all QuickJS classes registered).
 *
 * We simply point the worker's runtime handle at jcf->rt / jcf->ctx.
 * No new runtime, no re-evaluation, no file I/O.
 *
 * exit_process() frees the worker's private copy; exit_master() frees
 * the master's original — no double-free, no shared mutable state.
 */
static ngx_int_t
ngx_js_init_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t    *jcf;
    ngx_js_worker_t  *w;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    if (jcf->rt == NULL) {
        return NGX_OK;    /* no js_source directives */
    }

    w = ngx_pcalloc(cycle->pool, sizeof(ngx_js_worker_t));
    if (w == NULL) {
        return NGX_ERROR;
    }

    w->rt  = jcf->rt;
    w->ctx = jcf->ctx;

    /*
     * Measure the runtime's heap footprint right after fork.  This baseline
     * is stored in w->baseline_malloc_size so the content handler can compute
     * the absolute memory limit as baseline + nginx.workerMemoryLimit on each
     * request.  Both nginx.workerMemoryLimit and nginx.workerRequestTimeout
     * are read live from the JS property before every JS_Call so that request
     * handlers can reconfigure them at runtime.
     */
    {
        JSMemoryUsage  mu;

        JS_ComputeMemoryUsage(w->rt, &mu);
        w->baseline_malloc_size = (size_t) mu.malloc_size;
    }

    /*
     * Always install the interrupt handler so that request handlers can
     * set nginx.workerRequestTimeout at runtime and have it take effect
     * immediately.  The handler is a no-op when request_deadline_ms == 0.
     */
    JS_SetInterruptHandler(w->rt, ngx_js_interrupt_handler, w);

    /*
     * COMCON gas: the tenant runtime is a SEPARATE runtime, so it needs its own
     * interrupt handler to enforce the per-request execution budget (an
     * untrusted tenant `while(true){}` would otherwise hang the worker — memory
     * is already bounded by JS_SetMemoryLimit, but CPU time was not). Shares the
     * worker's request_deadline_ms, set around the tenant JS_Call in
     * ngx_js_tenant_content_handler.
     */
    if (jcf->tenant_rt != NULL) {
        JS_SetInterruptHandler(jcf->tenant_rt, ngx_js_interrupt_handler, w);
    }

    if (jcf->comcon_rt != NULL) {   /* M-CFG: gas for init-time include fragments */
        JS_SetInterruptHandler(jcf->comcon_rt, ngx_js_interrupt_handler, w);
    }

    /*
     * Overwrite the context opaque (set to cycle in ngx_js_com_init) with
     * the worker pointer so that JS C functions (e.g. nginx.setTimeout) can
     * retrieve the worker via JS_GetContextOpaque(ctx).
     */
    JS_SetContextOpaque(w->ctx, w);

    /* Update nginx.workerIdx to the actual 0-based worker index. */
    {
        JSValue  global, nginx_obj;

        global    = JS_GetGlobalObject(w->ctx);
        nginx_obj = JS_GetPropertyStr(w->ctx, global, "nginx");
        JS_FreeValue(w->ctx, global);

        if (!JS_IsException(nginx_obj) && !JS_IsUndefined(nginx_obj)) {
            JS_SetPropertyStr(w->ctx, nginx_obj, "workerIdx",
                              JS_NewInt32(w->ctx, (int32_t) ngx_worker));
            JS_FreeValue(w->ctx, nginx_obj);
        }
    }

    jcf->worker = w;

    /*
     * nginx.broadcast() callbacks are run by ngx_js_http_init_process()
     * (ngx_js_http_module, a later module), NOT here.  ngx_js_module is a
     * NGX_CORE_MODULE at a low index, so ngx_event_process_init() (which
     * calls ngx_event_timer_init and initialises free_connections) has not
     * yet run at this point.  Any nginx.setTimeout() or SharedWorker
     * postMessage() called from a broadcast callback therefore needs a live
     * event loop — see ngx_js_http_module.c:ngx_js_http_init_process.
     *
     * F4: store the per-worker bcast fd but do NOT register the event
     * here.  ngx_event_process_init (which initialises free_connections)
     * runs AFTER ngx_js_init_process, so ngx_get_connection would fail.
     * Lazy activation is done on the first request via
     * ngx_js_bcast_ensure_active().
     */
    w->bcast_fd   = ngx_js_sw_get_bcast_fd((ngx_uint_t) ngx_worker);
    w->bcast_conn = NULL;

    /*
     * Phase 2/3 messaging uses the existing nginx channel (ngx_channel).
     * NGX_CMD_JS_MESSAGE is handled by ngx_channel_handler via the
     * ngx_js_worker_channel_msg hook — no extra setup needed here.
     */

    return NGX_OK;
}


static void
ngx_js_exit_process(ngx_cycle_t *cycle)
{
    ngx_js_conf_t         *jcf;
    ngx_js_worker_t       *w;
    ngx_uint_t             i;
    ngx_js_socket_state_t *st;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    /* COMCON A3: the tenant runtime is COW-inherited like the host runtime;
     * free this process's copy. Independent of w — safe before the w check. */
    ngx_js_tenant_teardown(jcf);

    w = jcf->worker;
    if (w == NULL) {
        return;
    }

    /*
     * F3: close any worker-local sockets that were never activated.
     * Activated sockets (in_listening == 1) are nginx's responsibility
     * — their fd lives in cycle->listening and nginx will close them.
     * Unactivated sockets are this worker's private resource; leak them
     * and the fd is lost until the process exits.
     */
    for (i = 0; i < NGX_JS_LOCAL_SOCKET_REG_MAX; i++) {
        st = w->local_socket_reg[i];
        if (st == NULL) {
            continue;
        }

        if (!st->in_listening && st->fd >= 0) {
            (void) close(st->fd);
            st->fd = -1;
            ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                           "js: exit_process: closed worker-local socket %i",
                           (ngx_int_t) i);
        }

        ngx_free(st);
        w->local_socket_reg[i] = NULL;
    }

    /* F4: deregister bcast event handler */
    if (w->bcast_conn != NULL) {
        ngx_js_bcast_ctx_t  *bcast_ctx = w->bcast_conn->data;

        ngx_del_event(w->bcast_conn->read, NGX_READ_EVENT, 0);
        ngx_free_connection(w->bcast_conn);
        w->bcast_conn->fd = (ngx_socket_t) -1;
        w->bcast_conn     = NULL;

        if (bcast_ctx != NULL) {
            ngx_free(bcast_ctx);
        }
    }

    /*
     * Drain async-pending HTTP requests with 503 BEFORE closing SW sockets.
     *
     * On graceful shutdown (SIGQUIT) a suspended request waits for a SW reply.
     * If the SW thread dies before replying, the request hangs forever and the
     * worker never exits, deadlocking the whole shutdown sequence.
     *
     * Sending 503 here unblocks the request.  The drain must run while SW
     * channels are still open (before ngx_js_sw_exit_process) so that any
     * read event registered on a channel fd does not fire on a closed fd.
     *
     * ngx_js_async_drain_503 also frees the DupValue'd req_obj and promise,
     * satisfying the QuickJS "list_empty(&rt->gc_obj_list)" invariant.
     */
    if (w->ctx != NULL && w->async_pending != NULL) {
        ngx_js_async_drain_503(w);
        /* w->async_pending is now NULL */
    }

    ngx_js_sw_exit_process(cycle, jcf);

    /*
     * Drain body-filter, streaming-filter, and L4 pending entries.
     *
     * Like the async_pending drain above, this frees all DupValue'd
     * JSValues (satisfying the QuickJS "list_empty(&rt->gc_obj_list)"
     * invariant) and finalises the associated nginx connections so that
     * the "open socket left in connection" ALERT from
     * ngx_worker_process_exit() is not triggered on graceful shutdown.
     */
    if (w->ctx != NULL) {
        ngx_js_bf_pending_t  *bf_p, *bfnext;

        for (bf_p = w->bf_pending; bf_p != NULL; bf_p = bfnext) {
            bfnext = bf_p->next;

            ngx_log_error(NGX_LOG_WARN, bf_p->r->connection->log, 0,
                          "js: drain body-filter request with error on worker exit");

            if (!JS_IsUndefined(bf_p->gen)) {
                JS_FreeValue(w->ctx, bf_p->gen);
            }
            JS_FreeValue(w->ctx, bf_p->promise);

            /*
             * Response headers were already sent (the 200 OK went out before
             * the body-filter suspended).  We cannot send a new status line, so
             * just close the connection via NGX_DONE (count-- → 0 → close).
             * Setting c->error prevents the keepalive path from recycling it.
             */
            bf_p->r->connection->error = 1;
            ngx_http_finalize_request(bf_p->r, NGX_DONE);
        }
        w->bf_pending = NULL;

        {
            ngx_js_sf_pending_t  *sf_p, *sfnext;

            for (sf_p = w->sf_pending; sf_p != NULL; sf_p = sfnext) {
                sfnext = sf_p->next;

                ngx_log_error(NGX_LOG_WARN, sf_p->r->connection->log, 0,
                              "js: drain streaming-filter request with error"
                              " on worker exit");

                JS_FreeValue(w->ctx, sf_p->promise);

                sf_p->r->connection->error = 1;
                ngx_http_finalize_request(sf_p->r, NGX_DONE);
            }
            w->sf_pending = NULL;
        }

        ngx_js_l4_drain_exit(w);
        ngx_js_repl_drain_exit(w);
    }

    if (w->ctx) {
        /*
         * master_handlers is a GC-tracked JSValue held in the shared jcf.
         * Workers do not own it (only the master registers/fires it), but
         * they inherited a COW copy of the JS heap.  Each process has its
         * own private heap copy after the first write, so freeing it here
         * is safe — every process decrements its own ref_count independently.
         * This must happen before JS_FreeContext to avoid the QuickJS
         * "list_empty(&rt->gc_obj_list)" assertion on JS_FreeRuntime.
         */
        if (!JS_IsUninitialized(jcf->master_handlers)) {
            JS_FreeValue(w->ctx, jcf->master_handlers);
            jcf->master_handlers = JS_UNINITIALIZED;
        }

        JS_FreeContext(w->ctx);
        w->ctx = NULL;
        /* w->ctx aliases jcf->ctx (init_process). In single-process mode the
         * same jcf is torn down again by ngx_js_exit_master; null the shared
         * handle so that pass skips it (avoids a use-after-free). In
         * multi-process this jcf is the worker's private COW copy, so nulling
         * it here does not affect the master's teardown. */
        jcf->ctx = NULL;
    }

    if (w->rt) {
        js_std_free_handlers(w->rt);
        JS_FreeRuntime(w->rt);
        w->rt = NULL;
        jcf->rt = NULL;   /* aliases jcf->rt — see note above (single-process) */
    }
}


/*
 * ngx_js_init_module — called by ngx_init_cycle() after config parse.
 * Sets the global hook pointer so ngx_process_cycle.c can fire JS events
 * without including any JS headers.
 */

/* Defined in ngx_process_cycle.c — no JS headers needed there. */
extern void  (*ngx_js_master_event)(ngx_cycle_t *cycle, const char *event,
    ngx_pid_t pid, ngx_int_t slot, int status);
extern void  (*ngx_js_worker_channel_msg)(ngx_socket_t fd,
    ngx_int_t payload_len);
extern void  (*ngx_js_master_channel_msg)(ngx_cycle_t *cycle);
extern void  (*ngx_js_worker_load_plugin)(ngx_socket_t fd,    /* P16 */
    ngx_int_t payload_len);
extern void  (*ngx_js_sw_threads_start)(ngx_cycle_t *cycle);

static ngx_int_t
ngx_js_init_module(ngx_cycle_t *cycle)
{
    ngx_js_conf_t      *old_jcf, *new_jcf;
    static ngx_uint_t   atfork_registered;

    if (!atfork_registered) {
        pthread_atfork(ngx_js_atfork_prepare,
                       ngx_js_atfork_parent,
                       ngx_js_atfork_child);
        atfork_registered = 1;
    }

    /*
     * On reload: ngx_cycle still points to the OLD cycle here (nginx updates
     * ngx_cycle after init_module returns).  If the old cycle had JS scripts,
     * stop its SW threads and free its runtime now — before new workers fork.
     */
    if (ngx_cycle != NULL
        && ngx_cycle->conf_ctx != NULL
        && ngx_cycle != cycle)
    {
        old_jcf = (ngx_js_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                                  ngx_js_module);
        if (old_jcf != NULL) {

            /* Stop old SW threads; manager is a singleton, leave it running */
            ngx_js_sw_retire_threads(old_jcf);

            /* Redirect manager's dynamic-create list to the new config */
            new_jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                      ngx_js_module);
            if (new_jcf != NULL) {
                ngx_js_sw_update_mgr_jcf(new_jcf);
            }

            /* Free old master JS runtime (SW threads are joined — safe) */
            if (old_jcf->ctx != NULL) {
                if (!JS_IsUninitialized(old_jcf->master_handlers)) {
                    JS_FreeValue(old_jcf->ctx, old_jcf->master_handlers);
                    old_jcf->master_handlers = JS_UNINITIALIZED;
                }
                JS_FreeContext(old_jcf->ctx);
                old_jcf->ctx = NULL;
            }

            if (old_jcf->rt != NULL) {
                js_std_free_handlers(old_jcf->rt);
                JS_FreeRuntime(old_jcf->rt);
                old_jcf->rt = NULL;
            }

            /* COMCON A3: the old cycle's tenant runtime (reload leak guard) */
            ngx_js_tenant_teardown(old_jcf);
        }
    }

    /* Wire up master supervisory-loop hooks. */
    ngx_js_master_event       = ngx_js_dispatch_master_event;
    ngx_js_worker_channel_msg = ngx_js_handle_worker_channel_msg;
    ngx_js_master_channel_msg = ngx_js_handle_master_channel_msgs;
    /* P16: broadcast plugin-load from worker to this worker */
    ngx_js_worker_load_plugin = ngx_js_handle_worker_load_plugin;
    /* Start SW pthreads after daemonisation (not during init_conf) */
    ngx_js_sw_threads_start   = ngx_js_sw_threads_start_deferred;

    return NGX_OK;
}


/*
 * ngx_js_dispatch_master_event — invoked from the master supervisory loop
 * at key lifecycle points.  Looks up all JS handlers registered via
 * nginx.on(event, fn) and calls them synchronously.
 *
 * For 'workerSpawned': argv = [pid, slot]
 * For 'workerExited':  argv = [pid, slot, status]
 * For all others:      argv = []
 */
static void
ngx_js_dispatch_master_event(ngx_cycle_t *cycle, const char *event,
    ngx_pid_t pid, ngx_int_t slot, int status)
{
    ngx_js_conf_t  *jcf;
    JSContext      *ctx;
    JSValue         arr, len_val, fn, ret;
    JSValue         argv[3];
    uint32_t        i, len;
    int             argc;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);
    if (jcf == NULL || jcf->ctx == NULL) {
        return;
    }

    if (JS_IsUninitialized(jcf->master_handlers)) {
        return;
    }

    ctx = jcf->ctx;

    arr = JS_GetPropertyStr(ctx, jcf->master_handlers, event);
    if (JS_IsUndefined(arr) || !JS_IsArray(ctx, arr)) {
        JS_FreeValue(ctx, arr);
        return;
    }

    len_val = JS_GetPropertyStr(ctx, arr, "length");
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);

    if (strcmp(event, "workerSpawned") == 0) {
        argv[0] = JS_NewInt64(ctx, (int64_t) pid);
        argv[1] = JS_NewInt32(ctx, (int32_t) slot);
        argc = 2;

    } else if (strcmp(event, "workerExited") == 0) {
        argv[0] = JS_NewInt64(ctx, (int64_t) pid);
        argv[1] = JS_NewInt32(ctx, (int32_t) slot);
        argv[2] = JS_NewInt32(ctx, status);
        argc = 3;

    } else {
        argc = 0;
    }

    for (i = 0; i < len; i++) {
        fn = JS_GetPropertyUint32(ctx, arr, i);

        if (!JS_IsFunction(ctx, fn)) {
            JS_FreeValue(ctx, fn);
            continue;
        }

        ret = JS_Call(ctx, fn, JS_UNDEFINED, argc, argc ? argv : NULL);

        if (JS_IsException(ret)) {
            JSValue  exc;
            const char  *str;

            exc = JS_GetException(ctx);
            str = JS_ToCString(ctx, exc);
            ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                          "nginx.on('%s') handler exception: %s",
                          event, str ? str : "(null)");
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exc);
        }

        JS_FreeValue(ctx, ret);
        JS_FreeValue(ctx, fn);
    }

    /* free argv values */
    for (i = 0; i < (uint32_t) argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }

    JS_FreeValue(ctx, arr);

    /* drain any microtasks the handlers may have enqueued */
    {
        JSContext  *pctx;
        while (JS_ExecutePendingJob(jcf->rt, &pctx) > 0) { /* nothing */ }
    }
}


static void
ngx_js_exit_master(ngx_cycle_t *cycle)
{
    ngx_js_conf_t  *jcf;

    jcf = (ngx_js_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_js_module);

    ngx_js_sw_exit_master(jcf);

    ngx_js_tenant_teardown(jcf);          /* COMCON A3 */

    if (jcf->ctx) {
        if (!JS_IsUninitialized(jcf->master_handlers)) {
            JS_FreeValue(jcf->ctx, jcf->master_handlers);
            jcf->master_handlers = JS_UNINITIALIZED;
        }
        JS_FreeContext(jcf->ctx);
        jcf->ctx = NULL;
    }

    if (jcf->comcon_ctx != NULL) {   /* M-CFG: free frags, context, then its runtime */
        if (jcf->comcon_frags != NULL) {
            ngx_uint_t  fi;
            JSValue    *fv = jcf->comcon_frags->elts;
            for (fi = 0; fi < jcf->comcon_frags->nelts; fi++) {
                JS_FreeValue(jcf->comcon_ctx, fv[fi]);
            }
            jcf->comcon_frags->nelts = 0;
        }
        if (jcf->comcon_frags_pool != NULL) {
            ngx_destroy_pool(jcf->comcon_frags_pool);
            jcf->comcon_frags_pool = NULL;
            jcf->comcon_frags = NULL;
        }
        JS_FreeContext(jcf->comcon_ctx);
        jcf->comcon_ctx = NULL;
    }
    if (jcf->comcon_rt != NULL) {
        JS_FreeRuntime(jcf->comcon_rt);
        jcf->comcon_rt = NULL;
    }

    if (jcf->rt) {
        js_std_free_handlers(jcf->rt);
        JS_FreeRuntime(jcf->rt);
        jcf->rt = NULL;
    }
}


/*
 * config.write(text) — called from a js_preprocess script.
 *
 * Writes `text` to a temporary file then calls ngx_conf_parse()
 * recursively so that the generated text is treated as if it
 * appeared inline in nginx.conf.  Using a real file (rather than an
 * in-memory buffer) is required so that nested block directives
 * (http{}, server{}, etc.) are parsed correctly: ngx_conf_parse()
 * uses a non-invalid fd to distinguish parse_block from parse_param,
 * and only the file path gives that guarantee.
 *
 * The temporary file is unlinked before this function returns,
 * regardless of parse success or failure.
 */
JSValue
ngx_js_config_write(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    ngx_conf_t   *cf;
    const char   *text;
    size_t        len;
    char          tmppath[] = "/tmp/ngx_js_pp_XXXXXX";
    ngx_str_t     tmpstr;
    int           fd;
    ssize_t       n;
    char         *rv;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "config.write(text): argument required");
    }

    cf = JS_GetContextOpaque(ctx);
    if (cf == NULL) {
        return JS_ThrowInternalError(ctx, "config.write: no conf context");
    }

    text = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!text) {
        return JS_EXCEPTION;
    }

    fd = mkstemp(tmppath);
    if (fd == -1) {
        JS_FreeCString(ctx, text);
        return JS_ThrowInternalError(ctx, "config.write: mkstemp failed");
    }

    n = write(fd, text, len);
    close(fd);

    JS_FreeCString(ctx, text);

    if ((size_t) n != len) {
        unlink(tmppath);
        return JS_ThrowInternalError(ctx, "config.write: write failed");
    }

    tmpstr.data = (u_char *) tmppath;
    tmpstr.len  = ngx_strlen(tmppath);

    rv = ngx_conf_parse(cf, &tmpstr);

    unlink(tmppath);

    if (rv != NGX_CONF_OK) {
        return JS_ThrowInternalError(ctx,
                                     "config.write: config parse failed");
    }

    return JS_UNDEFINED;
}


/*
 * ngx_js_preprocess — directive handler for js_preprocess.
 *
 * Creates a short-lived JSRuntime, installs a global `config` object
 * exposing config.write(), evaluates the named script, then frees the
 * runtime.  The cf pointer is stored as the context opaque so that
 * config.write() can call ngx_conf_parse() directly.
 */
static char *
ngx_js_preprocess(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t   *value, path;
    u_char      *src;
    size_t       src_len;
    JSRuntime   *rt;
    JSContext   *ctx;
    JSValue      global, config_obj;
    char        *rv;

    value = cf->args->elts;
    path  = value[1];

    if (ngx_conf_full_name(cf->cycle, &path, 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    src = ngx_js_read_file(cf->cycle, &path, &src_len);
    if (src == NULL) {
        return NGX_CONF_ERROR;
    }

    rt = JS_NewRuntime();
    if (rt == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewRuntime() failed");
        return NGX_CONF_ERROR;
    }

    js_std_init_handlers(rt);
    JS_SetModuleLoaderFunc2(rt, NULL,
                            js_module_loader, js_module_check_attributes,
                            NULL);
    JS_SetSharedArrayBufferFunctions(rt, &ngx_js_sab_funcs);

    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: JS_NewContext() failed");
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    if (js_init_module_std(ctx, "std") == NULL
        || js_init_module_os(ctx, "os") == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "js: failed to register std/os modules");
        JS_FreeContext(ctx);
        js_std_free_handlers(rt);
        JS_FreeRuntime(rt);
        return NGX_CONF_ERROR;
    }

    /*
     * Store cf so that config.write() can retrieve it via
     * JS_GetContextOpaque() and call ngx_conf_parse().
     */
    JS_SetContextOpaque(ctx, cf);

    /* Install global `config` object with write() method */
    global     = JS_GetGlobalObject(ctx);
    config_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, config_obj, "write",
                      JS_NewCFunction(ctx, ngx_js_config_write, "write", 1));
    JS_SetPropertyStr(ctx, global, "config", config_obj);
    JS_FreeValue(ctx, global);

    rv = ngx_js_eval_module(ctx, rt, src, src_len, path.data, cf->log);

    JS_FreeContext(ctx);
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);

    return rv;
}


/*
 * Read an entire file into a NUL-terminated cycle->pool buffer.
 * Returns the buffer on success, NULL on error.
 */
u_char *
ngx_js_read_file(ngx_cycle_t *cycle, ngx_str_t *path, size_t *out_len)
{
    ngx_fd_t         fd;
    ngx_file_t       file;
    ngx_file_info_t  fi;
    u_char          *buf;
    size_t           size;
    ssize_t          n;

    fd = ngx_open_file(path->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: " ngx_open_file_n " \"%V\" failed", path);
        return NULL;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.fd   = fd;
    file.name = *path;
    file.log  = cycle->log;

    if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: " ngx_fd_info_n " \"%V\" failed", path);
        ngx_close_file(fd);
        return NULL;
    }

    size = ngx_file_size(&fi);

    buf = ngx_palloc(cycle->pool, size + 1);
    if (buf == NULL) {
        ngx_close_file(fd);
        return NULL;
    }

    n = ngx_read_file(&file, buf, size, 0);

    ngx_close_file(fd);

    if (n == NGX_ERROR) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "js: read \"%V\" failed", path);
        return NULL;
    }

    buf[n]   = '\0';
    *out_len = (size_t) n;

    return buf;
}
