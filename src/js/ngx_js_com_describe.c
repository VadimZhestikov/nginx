/*
 * ngx_js_com_describe.c — COM mutation safety-class metadata (Layer 1).
 *
 * Backs nginx.describe(path [, name]).  Each settable COM property/method is
 * classified along three orthogonal axes:
 *
 *   class         safe | guarded | irreversible | readonly  (the traffic light)
 *   propagation   worker-local | zoned-shared | auto-shared (the COW-trap axis)
 *   requestScoped honours setWriteMode('local') for a per-request override
 *
 * The tables below are the authoritative implementation of the spec in
 * js_com_docs/js-com-safety-classes.adoc and MUST match it member-for-member.
 *
 * A per-class_id registry maps each COM class to its table (and an optional
 * propagation-refine hook), so ngx_js_describe_members() stays fully generic.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <quickjs.h>
#include "ngx_js.h"
#include "ngx_js_com.h"


/* Convenience shorthands for table rows. */
#define RO   NGX_JS_CLS_READONLY
#define SAFE NGX_JS_CLS_SAFE
#define GRD  NGX_JS_CLS_GUARDED
#define IRR  NGX_JS_CLS_IRREVERSIBLE

#define REV  NGX_JS_MF_REVERSIBLE
#define RQS  NGX_JS_MF_REQUEST_SCOPED
#define METH NGX_JS_MF_METHOD   /* callable method; excluded from settable() */

#define WL   NGX_JS_PROP_WORKER_LOCAL
#define ZS   NGX_JS_PROP_ZONED_SHARED
#define AS   NGX_JS_PROP_AUTO_SHARED


/* ------------------------------------------------------------------ *
 * Classification tables                                               *
 * ------------------------------------------------------------------ *
 * One row per settable member.  Read-only members are intentionally
 * omitted (they carry no mutation safety class); describe() reports only
 * what these tables list.
 */

/* ------------------------------------------------------------------ *
 * M2b — typed signatures (first tranche: NginxLocation methods)        *
 * ------------------------------------------------------------------ *
 * Vocabulary is shared verbatim with mirror/lib/schema.js (see the
 * ngx_js_sig_t comment in ngx_js_com.h).  Signatures are optional and
 * trailing, so untyped rows are unaffected; describe() emits params[],
 * returns, returnsMem and effects[] only for members that have one.
 *
 * Return types below were read off the implementations, not assumed:
 * addHook/addResponseHook/clearHandler return JS_UNDEFINED (void);
 * remove/restoreLocation return JS_TRUE/JS_FALSE (bool); addLocation
 * returns a wrapped location (handle<NginxLocation>).
 */

/* Several structural ops accept either the key or the object it names
 * (ngx_js_coerce_key_arg) — expressed as a union rather than `any`. */
static const ngx_js_param_t  ngx_js_sig_p_lockey[] = {
    { "key", "str|handle<NginxLocation>", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_param_t  ngx_js_sig_p_lockey_opts[] = {
    { "key",  "str|handle<NginxLocation>", 0, "borrowed" },
    { "opts", "record",                    1, NULL },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_param_t  ngx_js_sig_p_fn[] = {
    { "fn", "handle<Function>", 0, NULL },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_param_t  ngx_js_sig_p_locspec[] = {
    { "spec", "record", 0, NULL },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_sig_t  ngx_js_sig_add_location = {
    ngx_js_sig_p_locspec, "handle<NginxLocation>", NULL, "mutate.location.tree"
};
static const ngx_js_sig_t  ngx_js_sig_remove_location = {
    ngx_js_sig_p_lockey_opts, "bool", NULL, "mutate.location.tree"
};
static const ngx_js_sig_t  ngx_js_sig_restore_location = {
    ngx_js_sig_p_lockey, "bool", NULL, "mutate.location.tree"
};
static const ngx_js_sig_t  ngx_js_sig_clear_handler = {
    NULL, "void", NULL, "restore.handler"
};
static const ngx_js_sig_t  ngx_js_sig_add_hook = {
    ngx_js_sig_p_fn, "void", NULL, "register.hook.precontent"
};
static const ngx_js_sig_t  ngx_js_sig_add_response_hook = {
    ngx_js_sig_p_fn, "void", NULL, "register.hook.response"
};

/*
 * M2d — remaining method tranches.  Parameter types were derived from how each
 * implementation actually reads its arguments (JS_IsArray / JS_IsFunction /
 * JS_IsObject / JS_ToCString*), and return types from its return statements —
 * not from the prose notes.  Every method below returns JS_UNDEFINED (void).
 */
static const ngx_js_param_t  ngx_js_sig_p_name_value[] = {
    { "name",  "str", 0, "borrowed" },
    { "value", "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_name[] = {
    { "name", "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_str[] = {
    { "value", "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_strlist[] = {
    { "values", "array<str>", 0, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_cert_key[] = {
    { "cert", "str", 0, "borrowed" },
    { "key",  "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_pairs[] = {
    { "pairs", "array<record>", 0, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_peerspec[] = {
    { "peer", "record", 0, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_event_fn[] = {
    { "event", "str",              0, "borrowed" },
    { "fn",    "handle<Function>", 0, NULL },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_sig_t  ngx_js_sig_add_header = {
    ngx_js_sig_p_name_value, "void", NULL, "set.response.header"
};
static const ngx_js_sig_t  ngx_js_sig_remove_header = {
    ngx_js_sig_p_name, "void", NULL, "set.response.header"
};
static const ngx_js_sig_t  ngx_js_sig_set_ciphers = {
    ngx_js_sig_p_str, "void", NULL, "mutate.ssl"
};
static const ngx_js_sig_t  ngx_js_sig_set_protocols = {
    ngx_js_sig_p_strlist, "void", NULL, "mutate.ssl"
};
static const ngx_js_sig_t  ngx_js_sig_set_certificate = {
    ngx_js_sig_p_cert_key, "void", NULL, "mutate.ssl"
};
static const ngx_js_sig_t  ngx_js_sig_set_pairs = {
    ngx_js_sig_p_pairs, "void", NULL, "mutate.sub_filter"
};
static const ngx_js_sig_t  ngx_js_sig_add_peer = {
    ngx_js_sig_p_peerspec, "void", NULL, "mutate.upstream.peers"
};
static const ngx_js_sig_t  ngx_js_sig_remove_peer = {
    ngx_js_sig_p_name, "void", NULL, "mutate.upstream.peers"
};
static const ngx_js_sig_t  ngx_js_sig_set_names = {
    ngx_js_sig_p_strlist, "void", NULL, "mutate.server.names"
};
static const ngx_js_sig_t  ngx_js_sig_on_event = {
    ngx_js_sig_p_event_fn, "void", NULL, "register.hook.event"
};
static const ngx_js_sig_t  ngx_js_sig_add_l4_filter = {
    ngx_js_sig_p_fn, "void", NULL, "register.filter.l4"
};
static const ngx_js_sig_t  ngx_js_sig_noargs_void = {
    NULL, "void", NULL, NULL
};


/*
 * M2e — the last untyped tranche: the structural operators on nginx.http, on
 * the two listener classes, plus location.clone() and charset.setCharset().
 * These are the highest-authority methods in the registry (they commit
 * listeners and servers into cycle->pool irreversibly), and until now they
 * were exactly the ones reviewCalls() had to report as `unchecked`.
 *
 * As before, every type below was read off the implementation, never off the
 * `note` prose — which was wrong at least once (see setCharset).
 *
 * Two deliberate decisions worth recording:
 *
 * 1. restoreServer/restoreListener are typed 1..1 even though their wrappers
 *    forward a 2-slot argv and therefore SWALLOW a second argument. Typing the
 *    tolerated arity instead of the real contract would bless
 *    `restoreServer(name, {hard:true})` — a call whose author plainly expects
 *    the option to do something, and for which nothing happens. Refusing it at
 *    admission is the entire point of the check. This is safe to do without
 *    grandfathering because contract.checkCalls is opt-in, so no proposal that
 *    admits today can start failing.
 *
 * 2. removeServer/removeListener take a duck-typed key: ngx_js_coerce_key_arg
 *    accepts ANY object carrying the right property (.name / .address), not a
 *    class-id-checked handle. The union says `str|handle<...>` because that is
 *    the intended contract, but note the runtime check is weaker than the type.
 */
static const ngx_js_param_t  ngx_js_sig_p_srvname_opts[] = {
    { "name", "str",    0, "borrowed" },
    { "opts", "record", 1, NULL },        /* { template: str } */
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_srvkey[] = {
    { "name", "str|handle<NginxServer>", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_srvkey_opts[] = {
    { "name", "str|handle<NginxServer>", 0, "borrowed" },
    { "opts", "record",                  1, NULL },   /* { hard: bool } */
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_socket[] = {
    { "socket", "handle<NginxSocket>", 0, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_addrkey[] = {
    { "address", "str|handle<NginxSocket>|handle<NginxListener>", 0,
      "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_addrkey_opts[] = {
    { "address", "str|handle<NginxSocket>|handle<NginxListener>", 0,
      "borrowed" },
    { "opts",    "record",                                        1, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_server[] = {
    { "server", "handle<NginxServer>", 0, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_stream_server[] = {
    { "server", "handle<NginxStreamServer>", 0, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_pattern_opts[] = {
    { "pattern", "str",    0, "borrowed" },
    { "opts",    "record", 1, NULL },     /* { depth: f64 } */
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_charset[] = {
    { "charset", "str", 0, "borrowed" },
    { "source",  "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_sig_t  ngx_js_sig_http_add_server = {
    ngx_js_sig_p_srvname_opts, "handle<NginxServer>", NULL, "mutate.server.tree"
};
static const ngx_js_sig_t  ngx_js_sig_http_remove_server = {
    ngx_js_sig_p_srvkey_opts, "bool", NULL, "mutate.server.tree"
};
static const ngx_js_sig_t  ngx_js_sig_http_restore_server = {
    ngx_js_sig_p_srvkey, "bool", NULL, "mutate.server.tree"
};
static const ngx_js_sig_t  ngx_js_sig_http_attach = {
    ngx_js_sig_p_socket, "handle<NginxHttpListener>", NULL, "mutate.listeners"
};
static const ngx_js_sig_t  ngx_js_sig_http_remove_listener = {
    ngx_js_sig_p_addrkey_opts, "bool", NULL, "mutate.listeners"
};
static const ngx_js_sig_t  ngx_js_sig_http_restore_listener = {
    ngx_js_sig_p_addrkey, "bool", NULL, "mutate.listeners"
};
/* Both return JS_DupValue(argv[0]) — the SAME server back, for chaining. */
static const ngx_js_sig_t  ngx_js_sig_listener_add_server = {
    ngx_js_sig_p_server, "handle<NginxServer>", NULL, "mutate.listeners"
};
static const ngx_js_sig_t  ngx_js_sig_listener_add_vserver = {
    ngx_js_sig_p_server, "handle<NginxServer>", NULL, "mutate.server.names"
};
static const ngx_js_sig_t  ngx_js_sig_stream_listener_add_server = {
    ngx_js_sig_p_stream_server, "handle<NginxStreamServer>", NULL,
    "mutate.listeners"
};
static const ngx_js_sig_t  ngx_js_sig_stream_listener_add_vserver = {
    ngx_js_sig_p_stream_server, "handle<NginxStreamServer>", NULL,
    "mutate.server.names"
};
static const ngx_js_sig_t  ngx_js_sig_location_clone = {
    ngx_js_sig_p_pattern_opts, "handle<NginxLocation>", NULL,
    "mutate.location.tree"
};
/* server.clone(name) is a DIFFERENT function from location.clone(pattern) —
 * ngx_js_server_fn_clone vs ngx_js_location_fn_clone, same spelling, different
 * arity and different blast radius.  Two rows, two signatures. */
static const ngx_js_param_t  ngx_js_sig_p_newname[] = {
    { "name", "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_sig_t  ngx_js_sig_server_clone = {
    ngx_js_sig_p_newname, "handle<NginxServer>", NULL,
    "mutate.server.tree,mutate.server.names"
};
static const ngx_js_sig_t  ngx_js_sig_http_add_hook = {
    ngx_js_sig_p_fn, "void", NULL, "register.hook.access"
};
static const ngx_js_sig_t  ngx_js_sig_set_charset = {
    ngx_js_sig_p_charset, "void", NULL, "mutate.charset"
};


/*
 * M2f — the methods that had NO row at all.
 *
 * `settable() subset describe()` only ever constrained PROPERTIES, and the
 * read-only discovery pass only finds prototype GETTERS, so a method was
 * invisible to both: neither in a table nor discoverable.  Walking the live
 * COM tree found 15 of these, and scanning own-property methods (not just the
 * prototype chain) found three more.
 *
 * Three of them are IRREVERSIBLE, which is the finding that matters here.
 * addUpstreamFilter, addUpstreamRequestFilter and onSelectPeer have NO inverse
 * anywhere in the tree -- there is no removeUpstreamFilter and no
 * offSelectPeer.  onSelectPeer additionally overwrites uscf->peer.init, the
 * upstream's load-balancer entry point, for the lifetime of the process, and
 * claims a slot in a fixed 64-entry static table that is never released.  An
 * operator reading the traffic light needs that to be red.
 *
 * The reads are classified RO, the first use of that class in these tables:
 * they are genuinely read-only members, but they are METHODS, so nothing
 * classified them before.
 */
static const ngx_js_param_t  ngx_js_sig_p_mode[] = {
    { "mode", "str", 0, "borrowed" },     /* "global" | "local" | "both" */
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_name_value_mode[] = {
    { "name",  "str", 0, "borrowed" },
    { "value", "any", 0, NULL },          /* whatever the target setter takes */
    { "mode",  "str", 1, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_name_mode[] = {
    { "name", "str", 0, "borrowed" },
    { "mode", "str", 1, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_fn_opts[] = {
    { "fn",   "handle<Function>", 0, NULL },
    { "opts", "record",           1, NULL },  /* {name,priority,index,before,after} */
    { NULL, NULL, 0, NULL }
};
/* addBodyFilter has two shapes: (asyncGenFn) or (mode, fn [, opts]). */
static const ngx_js_param_t  ngx_js_sig_p_body_filter[] = {
    { "modeOrFn", "str|handle<Function>",      0, "borrowed" },
    { "fnOrOpts", "handle<Function>|record",   1, NULL },
    { "opts",     "record",                    1, NULL },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_filter_ref[] = {
    { "filter", "str|handle<Function>", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_pattern[] = {
    { "pattern", "str", 0, "borrowed" },
    { NULL, NULL, 0, NULL }
};
static const ngx_js_param_t  ngx_js_sig_p_uri_server[] = {
    { "uri",        "str", 0, "borrowed" },
    { "serverName", "str", 1, "borrowed" },
    { NULL, NULL, 0, NULL }
};

static const ngx_js_sig_t  ngx_js_sig_set_write_mode = {
    ngx_js_sig_p_mode, "void", NULL, "set.write_mode"
};
static const ngx_js_sig_t  ngx_js_sig_set_read_mode = {
    ngx_js_sig_p_mode, "void", NULL, "set.read_mode"
};
static const ngx_js_sig_t  ngx_js_sig_set_property = {
    ngx_js_sig_p_name_value_mode, "void", NULL, "mutate.dynamic"
};
static const ngx_js_sig_t  ngx_js_sig_get_property = {
    ngx_js_sig_p_name_mode, "any", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_snapshot = {
    NULL, "handle<NginxSnapshot>", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_add_header_filter = {
    ngx_js_sig_p_fn_opts, "void", NULL, "register.filter.header"
};
static const ngx_js_sig_t  ngx_js_sig_add_body_filter = {
    ngx_js_sig_p_body_filter, "void", NULL, "register.filter.body"
};
static const ngx_js_sig_t  ngx_js_sig_add_upstream_filter = {
    ngx_js_sig_p_fn, "void", NULL, "register.filter.upstream"
};
static const ngx_js_sig_t  ngx_js_sig_add_upstream_req_filter = {
    ngx_js_sig_p_fn, "void", NULL, "register.filter.upstream.request"
};
static const ngx_js_sig_t  ngx_js_sig_remove_header_filter = {
    ngx_js_sig_p_filter_ref, "void", NULL, "register.filter.header"
};
static const ngx_js_sig_t  ngx_js_sig_remove_body_filter = {
    ngx_js_sig_p_filter_ref, "void", NULL, "register.filter.body"
};
static const ngx_js_sig_t  ngx_js_sig_get_header_filter = {
    ngx_js_sig_p_filter_ref, "record?", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_get_body_filter = {
    ngx_js_sig_p_filter_ref, "record?", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_find_location = {
    ngx_js_sig_p_pattern, "handle<NginxLocation>?", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_http_match = {
    ngx_js_sig_p_uri_server, "handle<NginxLocation>?", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_rebuild_vhost = {
    NULL, "void", NULL, "mutate.server.names"
};
static const ngx_js_sig_t  ngx_js_sig_server_by_name = {
    ngx_js_sig_p_newname, "handle<NginxServer>?", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_stream_server_by_name = {
    ngx_js_sig_p_newname, "handle<NginxStreamServer>?", NULL, NULL
};
static const ngx_js_sig_t  ngx_js_sig_on_select_peer = {
    ngx_js_sig_p_fn, "void", NULL, "mutate.upstream.balancer"
};


/*
 * `sig` (M2b) is a DELIBERATELY optional trailing field: a member without a
 * typed signature simply omits it and describe() emits the original 8-key
 * Descriptor.  nginx builds with -W -Werror, and -Wmissing-field-initializers
 * would otherwise reject all ~450 existing six-column rows.  Suppress just that
 * one diagnostic, scoped to the table block (pop'd after the last table) rather
 * than file-wide, so every other -Werror check still applies here.
 *
 * The alternative — a parallel name-keyed signature registry — was rejected: it
 * can silently disagree with the tables, which is exactly the drift this
 * typing work exists to remove.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

/* NginxLocation — core loc_conf scalars are all safe + request-scoped;
 * handler rewires dispatch (guarded, always-global); structural ops live
 * on the methods below. */
static const ngx_js_member_class_t  ngx_js_loc_members[] = {
    { "root",                     "string",  SAFE, REV|RQS, WL, NULL },
    { "handler",                  "function",GRD, REV,     WL,
      "Rewires nginx dispatch; always global; reverse with clearHandler()" },
    { "sendfile",                 "boolean", SAFE, REV|RQS, WL, NULL },
    { "tcpNopush",                "boolean", SAFE, REV|RQS, WL, NULL },
    { "tcpNodelay",               "boolean", SAFE, REV|RQS, WL, NULL },
    { "etag",                     "boolean", SAFE, REV|RQS, WL, NULL },
    { "keepaliveTimeout",         "number",  SAFE, REV|RQS, WL, NULL },
    { "keepaliveRequests",        "number",  SAFE, REV|RQS, WL, NULL },
    { "clientMaxBodySize",        "number",  SAFE, REV|RQS, WL, NULL },
    { "clientBodyTimeout",        "number",  SAFE, REV|RQS, WL, NULL },
    { "sendTimeout",              "number",  SAFE, REV|RQS, WL, NULL },
    { "defaultType",              "string",  SAFE, REV|RQS, WL, NULL },
    { "alias",                    "string",  SAFE, REV|RQS, WL,
      "Getter returns null for root-based locations" },
    { "satisfy",                  "string",  SAFE, REV|RQS, WL, NULL },
    { "limitExcept",              "string",  SAFE, REV|RQS, WL, NULL },
    { "lingering",                "string",  SAFE, REV|RQS, WL, NULL },
    { "lingeringTimeout",         "number",  SAFE, REV|RQS, WL, NULL },
    { "lingeringTime",            "number",  SAFE, REV|RQS, WL, NULL },
    { "resolverTimeout",          "number",  SAFE, REV|RQS, WL, NULL },
    { "chunkedTransferEncoding",  "boolean", SAFE, REV|RQS, WL, NULL },
    { "msieRefresh",              "boolean", SAFE, REV|RQS, WL, NULL },
    { "logNotFound",              "boolean", SAFE, REV|RQS, WL, NULL },
    { "logSubrequest",            "boolean", SAFE, REV|RQS, WL, NULL },
    { "recursiveErrorPages",      "boolean", SAFE, REV|RQS, WL, NULL },
    { "clientBodyBufferSize",     "number",  SAFE, REV|RQS, WL, NULL },
    { "clientBodyInFileOnly",     "boolean", SAFE, REV|RQS, WL, NULL },
    { "clientBodyInSingleBuffer", "boolean", SAFE, REV|RQS, WL, NULL },
    { "resetTimedoutConnection",  "boolean", SAFE, REV|RQS, WL, NULL },
    { "absoluteRedirect",         "boolean", SAFE, REV|RQS, WL, NULL },
    { "serverNameInRedirect",     "boolean", SAFE, REV|RQS, WL, NULL },
    { "portInRedirect",           "boolean", SAFE, REV|RQS, WL, NULL },
    { "msiePadding",              "boolean", SAFE, REV|RQS, WL, NULL },
    { "ifModifiedSince",          "string",  SAFE, REV|RQS, WL, NULL },
    { "maxRanges",                "number",  SAFE, REV|RQS, WL, NULL },
    { "authDelay",                "number",  SAFE, REV|RQS, WL, NULL },
    { "keepaliveTime",            "number",  SAFE, REV|RQS, WL, NULL },
    { "sendLowat",                "number",  SAFE, REV|RQS, WL, NULL },
    { "postponeOutput",           "number",  SAFE, REV|RQS, WL, NULL },
    { "keepaliveDisable",         "string",  SAFE, REV|RQS, WL, NULL },
    { "keepaliveMinTimeout",      "number",  SAFE, REV|RQS, WL, NULL },
    { "sendfileMaxChunk",         "number",  SAFE, REV|RQS, WL, NULL },
    { "readAhead",                "number",  SAFE, REV|RQS, WL, NULL },
    { "directio",                 "number",  SAFE, REV|RQS, WL, NULL },
    { "directioAlignment",        "number",  SAFE, REV|RQS, WL, NULL },
    { "errorPage",                "object[]",SAFE, REV|RQS, WL, NULL },
    { "addLocation",              "function",GRD, REV|METH,     WL,
      "Rebuilds live location BST; reverse with removeLocation",
      &ngx_js_sig_add_location },
    { "removeLocation",           "function",GRD, REV|METH,     WL,
      "Tombstone (reversible via restoreLocation); in-flight 404s not undone; "
      "{hard:true} for irreversible splice",
      &ngx_js_sig_remove_location },
    { "restoreLocation",          "function",GRD, REV|METH,     WL,
      "Clears a removeLocation tombstone; brings the route back",
      &ngx_js_sig_restore_location },
    /* Was ENTIRELY ABSENT from this table — registered as a method on
     * NginxLocation (ngx_js_com_http.c) but never classified, so describe()
     * reported nothing for it and reviewCalls() could not check it.  It
     * rebuilds the live location BST, same blast radius as addLocation. */
    { "clone",                    "function",GRD, REV|METH,     WL,
      "Clones matching locations under a new prefix and rebuilds the live "
      "location BST; idempotent (returns the existing location on a repeat); "
      "reverse with removeLocation",
      &ngx_js_sig_location_clone },
    { "clearHandler",             "function",SAFE, REV|METH,    WL,
      "Restores the location's original (pre-JS) handler",
      &ngx_js_sig_clear_handler },
    /*
     * addHook/addResponseHook were MISSING from this table even though both are
     * on NginxLocation's prototype, so describe() did not report them at all:
     * ngx_js_describe_append_readonly() only appends getter-only properties, and
     * a method absent from the table is invisible.  mirror's whole HTTP surface
     * is built on addHook, so this was a real hole in the classification.
     */
    { "addHook",                  "function",GRD, REV|METH,     WL,
      "Registers a pre-content hook (fn(r)); call r.respond() in it to cancel "
      "the chain. Rewires the location to the JS content handler",
      &ngx_js_sig_add_hook },
    { "addResponseHook",          "function",GRD, REV|METH,     WL,
      "Registers a response hook (fn(r)) run before headers are serialized",
      &ngx_js_sig_add_response_hook },

    /* --- M2f: read/write mode + generic property access ------------------ */
    { "setWriteMode",             "function",SAFE, REV|METH,    WL,
      "Selects where writes land: 'global' (shared loc_conf), 'local' "
      "(per-request copy in r->pool), or 'both'. Set on THIS wrapper, not the "
      "location — a second findLocation() of the same path starts at 'global'",
      &ngx_js_sig_set_write_mode },
    { "setReadMode",              "function",SAFE, REV|METH,    WL,
      "Selects where reads come from; 'both' is accepted but behaves as "
      "'local'. Default is 'global' for both modes",
      &ngx_js_sig_set_read_mode },
    { "setProperty",              "function",GRD, REV|METH|RQS, WL,
      "Generic setter (name, value [, mode]); dispatches to the named "
      "property, so its REAL safety class is that property's, not this row's. "
      "The mode override reaches this location only, not sub-object setters",
      &ngx_js_sig_set_property },
    { "getProperty",              "function",RO,  METH,         WL,
      "Generic getter (name [, mode]); return type is whatever the named "
      "property yields, so a caller cannot type the result statically",
      &ngx_js_sig_get_property },
    { "snapshot",                 "function",RO,  METH,         WL,
      "Captures the location's scalars plus proxy/gzip/headers/rewrite. Reads "
      "only — but the result is NOT detached: it holds live COM wrappers, and "
      "its restore() is the mutating half. Restore is best-effort, not exact",
      &ngx_js_sig_snapshot },

    /* --- M2f: filter registration ---------------------------------------- */
    { "addHeaderFilter",          "function",GRD, REV|METH,     WL,
      "Registers a header filter in the SHARED loc conf (never per-request, "
      "so a call during a request affects all later ones); reverse with "
      "removeHeaderFilter",
      &ngx_js_sig_add_header_filter },
    { "addBodyFilter",            "function",GRD, REV|METH,     WL,
      "Registers a body filter, either (asyncGenFn [, opts]) or "
      "(mode, fn [, opts]); reverse with removeBodyFilter",
      &ngx_js_sig_add_body_filter },
    /*
     * IRREVERSIBLE, and this is the point of classifying them: there is no
     * removeUpstreamFilter and no removeUpstreamRequestFilter anywhere in the
     * tree.  Once registered, an upstream filter runs for the process
     * lifetime.  They are also append-only (the priority field is stored but
     * never consulted on this path).
     */
    { "addUpstreamFilter",        "function",IRR, METH,         WL,
      "Registers an upstream response filter (async generator only). NO "
      "inverse exists — cannot be removed or replaced (irreversible)",
      &ngx_js_sig_add_upstream_filter },
    { "addUpstreamRequestFilter", "function",IRR, METH,         WL,
      "Registers an upstream request filter (async generator only). NO "
      "inverse exists — cannot be removed or replaced (irreversible)",
      &ngx_js_sig_add_upstream_req_filter },
    { "removeHeaderFilter",       "function",GRD, REV|METH,     WL,
      "Removes a header filter by name or function identity; returns nothing, "
      "so 'removed' and 'no such filter' are indistinguishable. Removing an "
      "INHERITED filter first materialises a private copy of the parent list",
      &ngx_js_sig_remove_header_filter },
    { "removeBodyFilter",         "function",GRD, REV|METH,     WL,
      "Removes a body filter by name or function identity; returns nothing, "
      "so 'removed' and 'no such filter' are indistinguishable",
      &ngx_js_sig_remove_body_filter },
    { "getHeaderFilter",          "function",RO,  METH,         WL,
      "Looks up a header filter; yields {name, priority, fn} or null",
      &ngx_js_sig_get_header_filter },
    { "getBodyFilter",            "function",RO,  METH,         WL,
      "Looks up a body filter; yields {name, priority, fn} or null — note the "
      "filter's MODE is not among them, so the result cannot round-trip a re-add",
      &ngx_js_sig_get_body_filter },
    { NULL, NULL, 0, 0, 0, NULL, NULL }
};

/* NginxProxy — all request-scoped; pass changes upstream selection (guarded) */
static const ngx_js_member_class_t  ngx_js_proxy_members[] = {
    { "pass",                "string",  GRD, REV|RQS, WL,
      "Re-targets the upstream for this location" },
    { "httpVersion",         "string",  SAFE, REV|RQS, WL, NULL },
    { "connectTimeout",      "number",  SAFE, REV|RQS, WL, NULL },
    { "sendTimeout",         "number",  SAFE, REV|RQS, WL, NULL },
    { "readTimeout",         "number",  SAFE, REV|RQS, WL, NULL },
    { "buffering",           "boolean", SAFE, REV|RQS, WL, NULL },
    { "requestBuffering",    "boolean", SAFE, REV|RQS, WL, NULL },
    { "interceptErrors",     "boolean", SAFE, REV|RQS, WL, NULL },
    { "bufferSize",          "number",  SAFE, REV|RQS, WL, NULL },
    { "nextUpstreamTries",   "number",  SAFE, REV|RQS, WL, NULL },
    { "nextUpstreamTimeout", "number",  SAFE, REV|RQS, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxGzip — all request-scoped safe scalars */
static const ngx_js_member_class_t  ngx_js_gzip_members[] = {
    { "enable",    "boolean", SAFE, REV|RQS, WL, NULL },
    { "level",     "number",  SAFE, REV|RQS, WL, NULL },
    { "minLength", "number",  SAFE, REV|RQS, WL, NULL },
    { "vary",      "boolean", SAFE, REV|RQS, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxHeaders — request-scoped; addHeaders is a pool-backed list (safe) */
static const ngx_js_member_class_t  ngx_js_headers_members[] = {
    { "addHeaders",      "object[]", SAFE, REV|RQS, WL,
      "Pool-backed list; atomic swap via the per-worker sub-pool registry" },
    { "addHeader",       "function", SAFE, REV|RQS|METH, WL,
      "Copy-on-write append into a fresh sub-pool",
      &ngx_js_sig_add_header },
    { "removeHeader",    "function", SAFE, REV|RQS|METH, WL, NULL,
      &ngx_js_sig_remove_header },
    { "headersInherit",  "string",   SAFE, REV|RQS, WL, NULL },
    { "trailersInherit", "string",   SAFE, REV|RQS, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxRewrite — all request-scoped safe scalars */
static const ngx_js_member_class_t  ngx_js_rewrite_members[] = {
    { "log",                       "boolean", SAFE, REV|RQS, WL, NULL },
    { "uninitializedVariableWarn", "boolean", SAFE, REV|RQS, WL, NULL },
    { "stackSize",                 "number",  SAFE, REV|RQS, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxPeer / NginxRrPeer — scalar setters; propagation is zoned-shared when
 * the upstream is zone-backed (resolved live by the refine hook below). */
static const ngx_js_member_class_t  ngx_js_peer_members[] = {
    { "weight",      "number",  SAFE, REV, ZS,
      "Recomputes total_weight under rr_peers_wlock" },
    { "maxFails",    "number",  SAFE, REV, ZS, NULL },
    { "down",        "boolean", SAFE, REV, ZS, "Adjusts peers->tries" },
    { "failTimeout", "number",  SAFE, REV, ZS, NULL },
    { "maxConns",    "number",  SAFE, REV, ZS, NULL },
    { "snapshot",    "function", RO, METH, ZS,
      "Captures this peer's weight/maxFails/down/failTimeout/maxConns. Reads "
      "only — but NOT detached: it pins the live peer wrapper and restore() "
      "writes back through the setters. On a zoned RR peer that write goes to "
      "SHARED memory under the rr_peers wlock, visible to every worker, and "
      "nothing pins the peer against a concurrent removePeer",
      &ngx_js_sig_snapshot },
    { NULL, NULL, 0, 0, 0, NULL }
};


/* ---- Global-only sub-objects (safe scalar setters; no request scope) ---- */

/* NginxSSL / NginxStreamSSL — set* mutate the live SSL_CTX (guarded). */
static const ngx_js_member_class_t  ngx_js_ssl_members[] = {
    { "sessionTimeout",      "number",  SAFE, REV, WL, NULL },
    { "sessionTickets",      "boolean", SAFE, REV, WL, NULL },
    { "preferServerCiphers", "boolean", SAFE, REV, WL, NULL },
    { "verifyDepth",         "number",  SAFE, REV, WL, NULL },
    { "handshakeTimeout",    "number",  SAFE, REV, WL, NULL },
    { "setCiphers",          "function",GRD, REV|METH, WL, "Mutates live SSL_CTX",
      &ngx_js_sig_set_ciphers },
    { "setProtocols",        "function",GRD, REV|METH, WL, "Mutates live SSL_CTX",
      &ngx_js_sig_set_protocols },
    { "setCertificate",      "function",GRD, REV|METH, WL,
      "Hot-swaps the live certificate + key",
      &ngx_js_sig_set_certificate },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_proxy_cache_members[] = {
    { "minUses",          "number",  SAFE, REV, WL, NULL },
    { "lock",             "boolean", SAFE, REV, WL, NULL },
    { "lockTimeout",      "number",  SAFE, REV, WL, NULL },
    { "lockAge",          "number",  SAFE, REV, WL, NULL },
    { "revalidate",       "boolean", SAFE, REV, WL, NULL },
    { "convertHead",      "boolean", SAFE, REV, WL, NULL },
    { "backgroundUpdate", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxAccess / NginxStreamAccess share this table. */
static const ngx_js_member_class_t  ngx_js_access_members[] = {
    { "rules",     "object[]", SAFE, REV, WL, NULL },
    { "rules6",    "object[]", SAFE, REV, WL, NULL },
    { "rulesUnix", "object[]", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_auth_members[] = {
    { "realm",    "string", SAFE, REV, WL, NULL },
    { "userFile", "string", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_limit_req_members[] = {
    { "logLevel",      "string",  SAFE, REV, WL, NULL },
    { "delayLogLevel", "string",  SAFE, REV, WL, NULL },
    { "statusCode",    "number",  SAFE, REV, WL, NULL },
    { "dryRun",        "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_limit_req_limit_members[] = {
    { "burst",   "number",  SAFE, REV, WL, NULL },
    { "nodelay", "boolean", SAFE, REV, WL, NULL },
    { "delay",   "number",  SAFE, REV, WL, NULL },
    { "rate",    "number",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_limit_conn_members[] = {
    { "logLevel",   "string",  SAFE, REV, WL, NULL },
    { "statusCode", "number",  SAFE, REV, WL, NULL },
    { "dryRun",     "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_fastcgi_members[] = {
    { "index",            "string",  SAFE, REV, WL, NULL },
    { "keepConn",         "boolean", SAFE, REV, WL, NULL },
    { "connectTimeout",   "number",  SAFE, REV, WL, NULL },
    { "sendTimeout",      "number",  SAFE, REV, WL, NULL },
    { "readTimeout",      "number",  SAFE, REV, WL, NULL },
    { "buffering",        "boolean", SAFE, REV, WL, NULL },
    { "requestBuffering", "boolean", SAFE, REV, WL, NULL },
    { "interceptErrors",  "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_log_members[] = {
    { "off", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_realip_members[] = {
    { "recursive", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_charset_members[] = {
    { "overrideCharset", "boolean", SAFE, REV, WL, NULL },
    { "setCharset",      "function",SAFE, REV|METH, WL,
      "Sets source + destination charset; both arguments are required",
      &ngx_js_sig_set_charset },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_sub_filter_members[] = {
    { "once",         "boolean", SAFE, REV, WL, NULL },
    { "lastModified", "boolean", SAFE, REV, WL, NULL },
    { "setPairs",     "function",SAFE, REV|METH, WL, "Replaces substitution pairs",
      &ngx_js_sig_set_pairs },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_autoindex_members[] = {
    { "enable",    "boolean", SAFE, REV, WL, NULL },
    { "format",    "string",  SAFE, REV, WL, NULL },
    { "localtime", "boolean", SAFE, REV, WL, NULL },
    { "exactSize", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_referer_members[] = {
    { "noReferer",      "boolean", SAFE, REV, WL, NULL },
    { "blockedReferer", "boolean", SAFE, REV, WL, NULL },
    { "serverNames",    "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_dav_members[] = {
    { "methods",           "string[]", SAFE, REV, WL, NULL },
    { "access",            "number",   SAFE, REV, WL, NULL },
    { "minDeleteDepth",    "number",   SAFE, REV, WL, NULL },
    { "createFullPutPath", "boolean",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_ssi_members[] = {
    { "enable",                "boolean", SAFE, REV, WL, NULL },
    { "silentErrors",          "boolean", SAFE, REV, WL, NULL },
    { "ignoreRecycledBuffers", "boolean", SAFE, REV, WL, NULL },
    { "lastModified",          "boolean", SAFE, REV, WL, NULL },
    { "minFileChunk",          "number",  SAFE, REV, WL, NULL },
    { "valueLen",              "number",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_userid_members[] = {
    { "enable", "string", SAFE, REV, WL, NULL },
    { "name",   "string", SAFE, REV, WL, NULL },
    { "domain", "string", SAFE, REV, WL, NULL },
    { "path",   "string", SAFE, REV, WL, NULL },
    { "p3p",    "string", SAFE, REV, WL, NULL },
    { "mark",   "string", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_addition_members[] = {
    { "addBeforeBody", "string", SAFE, REV, WL, NULL },
    { "addAfterBody",  "string", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_gunzip_members[] = {
    { "enable",  "boolean", SAFE, REV, WL, NULL },
    { "buffers", "object",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_slice_members[] = {
    { "size", "number", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_image_filter_members[] = {
    { "action",       "string",  SAFE, REV, WL, NULL },
    { "width",        "number",  SAFE, REV, WL, NULL },
    { "height",       "number",  SAFE, REV, WL, NULL },
    { "angle",        "number",  SAFE, REV, WL, NULL },
    { "jpegQuality",  "number",  SAFE, REV, WL, NULL },
    { "webpQuality",  "number",  SAFE, REV, WL, NULL },
    { "sharpen",      "number",  SAFE, REV, WL, NULL },
    { "transparency", "boolean", SAFE, REV, WL, NULL },
    { "interlace",    "boolean", SAFE, REV, WL, NULL },
    { "bufferSize",   "number",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_xslt_members[] = {
    { "lastModified", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_secure_link_members[] = {
    { "secret", "string", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_mp4_members[] = {
    { "bufferSize",    "number",  SAFE, REV, WL, NULL },
    { "maxBufferSize", "number",  SAFE, REV, WL, NULL },
    { "startKeyFrame", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_random_index_members[] = {
    { "enable", "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_auth_request_members[] = {
    { "uri", "string", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_gzip_static_members[] = {
    { "enable", "string", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_memcached_members[] = {
    { "connectTimeout", "number", SAFE, REV, WL, NULL },
    { "sendTimeout",    "number", SAFE, REV, WL, NULL },
    { "readTimeout",    "number", SAFE, REV, WL, NULL },
    { "bufferSize",     "number", SAFE, REV, WL, NULL },
    { "gzipFlag",       "number", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_scgi_members[] = {
    { "connectTimeout", "number", SAFE, REV, WL, NULL },
    { "sendTimeout",    "number", SAFE, REV, WL, NULL },
    { "readTimeout",    "number", SAFE, REV, WL, NULL },
    { "bufferSize",     "number", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_uwsgi_members[] = {
    { "connectTimeout", "number", SAFE, REV, WL, NULL },
    { "sendTimeout",    "number", SAFE, REV, WL, NULL },
    { "readTimeout",    "number", SAFE, REV, WL, NULL },
    { "bufferSize",     "number", SAFE, REV, WL, NULL },
    { "modifier1",      "number", SAFE, REV, WL, NULL },
    { "modifier2",      "number", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_mirror_members[] = {
    { "uris",        "string[]", SAFE, REV, WL, NULL },
    { "requestBody", "boolean",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_events_members[] = {
    { "multiAccept",      "boolean", SAFE, REV, WL, NULL },
    { "acceptMutex",      "boolean", SAFE, REV, WL, NULL },
    { "acceptMutexDelay", "number",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxHttp — nginx.http is a plain JS object (not a class instance), so
 * describe() reaches it via the hidden NGX_JS_DTAG_HTTP tag (see the tag
 * registry below) rather than a JSClassID.  These are the topology methods —
 * the safety-honesty gap they used to leave unclassified (follow-up #2). */
static const ngx_js_member_class_t  ngx_js_http_members[] = {
    { "addServer",       "function", IRR, METH,   WL,
      "Adds a server; its cscf is committed in cycle->pool and never reclaimed "
      "for the process lifetime (irreversible)",
      &ngx_js_sig_http_add_server },
    { "removeServer",    "function", GRD, REV|METH, WL,
      "Tombstone (reversible via restoreServer); {hard:true} for irreversible "
      "splice",
      &ngx_js_sig_http_remove_server },
    { "restoreServer",   "function", GRD, REV|METH, WL,
      "Clears a removeServer tombstone; brings the virtual server back",
      &ngx_js_sig_http_restore_server },
    { "attach",          "function", IRR, METH,   WL,
      "Binds a createSocket() fd into cycle->listening; a committed resource, "
      "never reclaimed at runtime (irreversible)",
      &ngx_js_sig_http_attach },
    { "removeListener",  "function", GRD, REV|METH, WL,
      "Soft pause (reversible via restoreListener); {hard:true} closes the "
      "socket — connections refused, port freed (irreversible)",
      &ngx_js_sig_http_remove_listener },
    { "restoreListener", "function", GRD, REV|METH, WL,
      "Re-arms a soft-paused listener; returns false after a {hard:true} close",
      &ngx_js_sig_http_restore_listener },
    { "addHook",         "function", GRD, REV|METH, WL,
      "Registers a global access-phase hook; reverse by clearing it",
      &ngx_js_sig_http_add_hook },
    { "match",           "function", RO,  METH,     WL,
      "Simulates nginx location matching for a URI on a server and yields the "
      "location that would handle it, or null; changes nothing",
      &ngx_js_sig_http_match },
    { "rebuildVhostDispatch", "function", GRD, REV|METH, WL,
      "Recomputes the virtual-server hash from the current server set — "
      "idempotent in effect, but each call allocates a fresh hash in "
      "cycle->pool that is never reclaimed, so repeated runtime calls grow it",
      &ngx_js_sig_rebuild_vhost },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxUpstream (HTTP) — live RR peer topology; zoned-shared like the stream
 * upstream's (cross-worker iff the upstream is zone-backed). */
static const ngx_js_member_class_t  ngx_js_upstream_members[] = {
    { "addPeer",    "function", GRD, REV|METH, ZS,
      "Adds a live RR peer (under rr_peers wlock when zone-backed)",
      &ngx_js_sig_add_peer },
    { "removePeer", "function", GRD, REV|METH, ZS, "Reverse with addPeer()",
      &ngx_js_sig_remove_peer },
    { "onSelectPeer", "function", IRR, METH, WL,
      "Replaces the upstream's load balancer (uscf->peer.init) with a JS hook "
      "for the lifetime of the process. NO inverse exists, and the hook claims "
      "one of 64 fixed static slots that is never released (irreversible)",
      &ngx_js_sig_on_select_peer },
    { "snapshot",   "function", RO,  METH, ZS,
      "Captures every peer's five tunables, one node per peer. Reads only, but "
      "materialises and pins a wrapper per peer; restore() reconciles nothing "
      "if peers were added or removed in between",
      &ngx_js_sig_snapshot },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxSocket — createSocket() handle. */
static const ngx_js_member_class_t  ngx_js_socket_members[] = {
    { "close",     "function", GRD, REV|METH, WL,
      "Closes the fd and unregisters; recreate with nginx.createSocket()",
      &ngx_js_sig_noargs_void },
    { "broadcast", "function", GRD, REV|METH, WL,
      "Distributes the fd to all workers via the manager thread",
      &ngx_js_sig_noargs_void },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxHttpListener — nginx.http.attach() handle.  addServer / addVirtualServer
 * activate the listener (push into cycle->listening / rebuild virtual_names),
 * a committed resource → irreversible; the hook/filter registrations are
 * guarded and reversible. */
static const ngx_js_member_class_t  ngx_js_http_listener_members[] = {
    { "addServer",        "function", IRR, METH,   WL,
      "Activates the listener (cycle->listening); committed (irreversible)",
      &ngx_js_sig_listener_add_server },
    { "addVirtualServer", "function", IRR, METH,   WL,
      "Rebuilds virtual_names host routing; committed (irreversible)",
      &ngx_js_sig_listener_add_vserver },
    { "serverByName",     "function", RO,  METH,   WL,
      "Resolves a server name on this listener (case-insensitive) to its "
      "NginxServer, or null; changes nothing",
      &ngx_js_sig_server_by_name },
    { "on",               "function", GRD, REV|METH, WL,
      "Registers an accept hook",
      &ngx_js_sig_on_event },
    { "addL4Filter",      "function", GRD, REV|METH, WL,
      "Registers a raw inbound TCP filter",
      &ngx_js_sig_add_l4_filter },
    { "addL4SendFilter",  "function", GRD, REV|METH, WL,
      "Registers a raw outbound TCP filter",
      &ngx_js_sig_add_l4_filter },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxCycle — master-level scalars. */
static const ngx_js_member_class_t  ngx_js_cycle_members[] = {
    { "workers", "number", GRD, REV, WL,
      "Sets worker_processes; takes effect only when the master next spawns "
      "workers (e.g. on reload)" },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxStreamServer — config-phase scalars + the content handler. */
static const ngx_js_member_class_t  ngx_js_stream_server_members[] = {
    { "tcpNodelay",           "boolean",  SAFE, REV, WL, NULL },
    { "prereadBufferSize",    "number",   SAFE, REV, WL, NULL },
    { "prereadTimeout",       "number",   SAFE, REV, WL, NULL },
    { "resolverTimeout",      "number",   SAFE, REV, WL, NULL },
    { "proxyProtocolTimeout", "number",   SAFE, REV, WL, NULL },
    { "handler",              "function", GRD,  REV, WL,
      "Rewires stream dispatch to a JS content handler; always global" },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxStreamProxy — all scalar setters, reversible. */
static const ngx_js_member_class_t  ngx_js_stream_proxy_members[] = {
    { "connectTimeout",      "number",  SAFE, REV, WL, NULL },
    { "timeout",             "number",  SAFE, REV, WL, NULL },
    { "nextUpstreamTimeout", "number",  SAFE, REV, WL, NULL },
    { "bufferSize",          "number",  SAFE, REV, WL, NULL },
    { "nextUpstreamTries",   "number",  SAFE, REV, WL, NULL },
    { "nextUpstream",        "boolean", SAFE, REV, WL, NULL },
    { "proxyProtocol",       "boolean", SAFE, REV, WL, NULL },
    { "halfClose",           "boolean", SAFE, REV, WL, NULL },
    { "socketKeepalive",     "boolean", SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxStreamListener — stream.attach() handle; activation is irreversible. */
static const ngx_js_member_class_t  ngx_js_stream_listener_members[] = {
    { "addServer",        "function", IRR, METH, WL,
      "Activates the listener (cycle->listening); committed (irreversible)",
      &ngx_js_sig_stream_listener_add_server },
    { "addVirtualServer", "function", IRR, METH, WL,
      "Rebuilds stream virtual_names routing; committed (irreversible)",
      &ngx_js_sig_stream_listener_add_vserver },
    { "serverByName",     "function", RO,  METH, WL,
      "Resolves a server name on this stream listener to its "
      "NginxStreamServer, or null; changes nothing",
      &ngx_js_sig_stream_server_by_name },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxSnapshot — restore re-applies captured property values. */
static const ngx_js_member_class_t  ngx_js_snapshot_members[] = {
    { "restore", "function", GRD, REV|METH, WL,
      "Re-applies captured property values; reversible by re-snapshotting",
      &ngx_js_sig_noargs_void },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxServer — scalar setters safe; names/dispatch/topology are guarded or
 * irreversible. */
static const ngx_js_member_class_t  ngx_js_server_members[] = {
    { "root",                   "string",  SAFE, REV, WL, NULL },
    { "clientHeaderBufferSize", "number",  SAFE, REV, WL, NULL },
    { "clientHeaderTimeout",    "number",  SAFE, REV, WL, NULL },
    { "ignoreInvalidHeaders",   "boolean", SAFE, REV, WL, NULL },
    { "mergeSlashes",           "boolean", SAFE, REV, WL, NULL },
    { "underscoresInHeaders",   "boolean", SAFE, REV, WL, NULL },
    { "serverTokens",           "string",  SAFE, REV, WL, NULL },
    { "connectionPoolSize",     "number",  SAFE, REV, WL, NULL },
    { "requestPoolSize",        "number",  SAFE, REV, WL, NULL },
    { "setNames",               "function",GRD, REV|METH, WL,
      "Takes effect only after rebuildVhostDispatch()",
      &ngx_js_sig_set_names },
    /*
     * NginxServer's structural ops delegate to the same ngx_js_do_* helpers as
     * NginxLocation's (ngx_js_server_fn_add_location -> ngx_js_do_add_location,
     * etc.), so they share the signatures verbatim rather than restating them.
     */
    { "addLocation",            "function",GRD, REV|METH, WL,
      "Rebuilds live BST; reverse with removeLocation",
      &ngx_js_sig_add_location },
    { "removeLocation",         "function",GRD, REV|METH, WL,
      "Tombstone (reversible via restoreLocation); in-flight 404s not undone; "
      "{hard:true} for irreversible splice",
      &ngx_js_sig_remove_location },
    { "restoreLocation",        "function",GRD, REV|METH, WL,
      "Clears a removeLocation tombstone",
      &ngx_js_sig_restore_location },
    /*
     * MISCLASSIFIED until M2e: this was SAFE + reversible, noted as "produces a
     * detached config object; no live effect until added".  It is neither.
     * ngx_js_server_fn_clone allocates the new cscf and its whole conf_ctx from
     * cycle->pool (never reclaimed for the process lifetime) and then splices
     * new_cscf into EVERY vhost dispatch entry, so the cloned server is live and
     * routable by Host the moment clone() returns.  That is strictly more than
     * addServer() does, and addServer is IRR on exactly this rationale.
     *
     * The whole point of the safety-class layer is that an operator — or a
     * mediated tenant consulting the traffic light — can trust it; a green light
     * on a permanent, immediately-routable server commit is the failure this
     * layer exists to prevent.  Reclassified to match addServer.
     */
    { "findLocation",           "function",RO,  METH,     WL,
      "Looks up a location by pattern (with its nginx modifier: '= ', '^~ ', "
      "'~ ', '~* ', '@'); the modifier needs its trailing space. Tombstoned "
      "(removed) locations are deliberately not findable. Returns a FRESH "
      "wrapper each call, so read/write modes set on one result do not carry",
      &ngx_js_sig_find_location },
    { "clone",                  "function",IRR, METH,     WL,
      "Clones this server under a new name; the new cscf is committed in "
      "cycle->pool and spliced into every vhost dispatch entry — live and "
      "routable immediately, never reclaimed (irreversible)",
      &ngx_js_sig_server_clone },
    { "addHook",                "function",GRD, REV|METH, WL, NULL,
      &ngx_js_sig_add_hook },
    { "on",                     "function",GRD, REV|METH, WL, NULL,
      &ngx_js_sig_on_event },
    { "addL4Filter",            "function",GRD, REV|METH, WL, NULL,
      &ngx_js_sig_add_l4_filter },
    { "addL4SendFilter",        "function",GRD, REV|METH, WL, NULL,
      &ngx_js_sig_add_l4_filter },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxStreamPeer (config-phase, no zone lock → worker-local). */
static const ngx_js_member_class_t  ngx_js_stream_peer_members[] = {
    { "weight",      "number",  SAFE, REV, WL, NULL },
    { "maxFails",    "number",  SAFE, REV, WL, NULL },
    { "down",        "boolean", SAFE, REV, WL, NULL },
    { "failTimeout", "number",  SAFE, REV, WL, NULL },
    { "maxConns",    "number",  SAFE, REV, WL, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxStreamRrPeer (runtime; zoned-shared resolved by refine hook). */
static const ngx_js_member_class_t  ngx_js_stream_rr_peer_members[] = {
    { "weight",      "number",  SAFE, REV, ZS,
      "Recomputes total_weight under stream rr_peers_wlock" },
    { "maxFails",    "number",  SAFE, REV, ZS, NULL },
    { "down",        "boolean", SAFE, REV, ZS, "Adjusts peers->tries" },
    { "failTimeout", "number",  SAFE, REV, ZS, NULL },
    { "maxConns",    "number",  SAFE, REV, ZS, NULL },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_stream_upstream_members[] = {
    { "addPeer",    "function", GRD, REV|METH, ZS, "Adds a live RR peer",
      &ngx_js_sig_add_peer },
    { "removePeer", "function", GRD, REV|METH, ZS, "Reverse with addPeer()",
      &ngx_js_sig_remove_peer },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* end of the classification tables — restore -Wmissing-field-initializers */
#pragma GCC diagnostic pop


/* ------------------------------------------------------------------ *
 * Propagation refine hooks                                            *
 * ------------------------------------------------------------------ */

/*
 * Runtime RR peer: zoned-shared only when the upstream has a shm zone;
 * otherwise the peer struct is worker-local and the lock is a no-op.
 */
static ngx_js_propagation_e
ngx_js_peer_prop_refine(JSContext *ctx, JSValueConst obj,
    const ngx_js_member_class_t *m)
{
    (void) ctx; (void) m;
    return ngx_js_rr_peer_is_zoned(obj) ? NGX_JS_PROP_ZONED_SHARED
                                        : NGX_JS_PROP_WORKER_LOCAL;
}


/* Stream runtime RR peer — same zoned-shared vs worker-local distinction. */
static ngx_js_propagation_e
ngx_js_stream_peer_prop_refine(JSContext *ctx, JSValueConst obj,
    const ngx_js_member_class_t *m)
{
    (void) ctx; (void) m;
    return ngx_js_stream_rr_peer_is_zoned(obj) ? NGX_JS_PROP_ZONED_SHARED
                                               : NGX_JS_PROP_WORKER_LOCAL;
}


/* ------------------------------------------------------------------ *
 * Per-class_id registry                                               *
 * ------------------------------------------------------------------ *
 * Maps each COM class to its member table.  class_id globals are filled at
 * runtime (JS_NewClassID) but are valid by the time describe() is called, so
 * we store their addresses and dereference at lookup.  Add one row per class.
 */
typedef struct {
    JSClassID                    *cid;
    const ngx_js_member_class_t  *table;
    ngx_js_prop_refine_pt         refine;
} ngx_js_member_registry_t;

static const ngx_js_member_registry_t  ngx_js_member_registry[] = {
    { &ngx_js_location_class_id, ngx_js_loc_members,     NULL },
    { &ngx_js_proxy_class_id,    ngx_js_proxy_members,   NULL },
    { &ngx_js_gzip_class_id,     ngx_js_gzip_members,    NULL },
    { &ngx_js_headers_class_id,  ngx_js_headers_members, NULL },
    { &ngx_js_rewrite_class_id,  ngx_js_rewrite_members, NULL },
    { &ngx_js_rr_peer_class_id,  ngx_js_peer_members, ngx_js_peer_prop_refine },
    { &ngx_js_peer_class_id,     ngx_js_peer_members,    NULL },

    /* Server + global-only location sub-objects */
    { &ngx_js_server_class_id,       ngx_js_server_members,       NULL },
    { &ngx_js_ssl_class_id,          ngx_js_ssl_members,          NULL },
    { &ngx_js_proxy_cache_class_id,  ngx_js_proxy_cache_members,  NULL },
    { &ngx_js_access_class_id,       ngx_js_access_members,       NULL },
    { &ngx_js_auth_class_id,         ngx_js_auth_members,         NULL },
    { &ngx_js_limit_req_class_id,    ngx_js_limit_req_members,    NULL },
    { &ngx_js_limit_req_limit_class_id, ngx_js_limit_req_limit_members, NULL },
    { &ngx_js_limit_conn_class_id,   ngx_js_limit_conn_members,   NULL },
    { &ngx_js_fastcgi_class_id,      ngx_js_fastcgi_members,      NULL },
    { &ngx_js_log_class_id,          ngx_js_log_members,          NULL },
    { &ngx_js_realip_class_id,       ngx_js_realip_members,       NULL },
    { &ngx_js_charset_class_id,      ngx_js_charset_members,      NULL },
    { &ngx_js_sub_filter_class_id,   ngx_js_sub_filter_members,   NULL },
    { &ngx_js_autoindex_class_id,    ngx_js_autoindex_members,    NULL },
    { &ngx_js_referer_class_id,      ngx_js_referer_members,      NULL },
    { &ngx_js_dav_class_id,          ngx_js_dav_members,          NULL },
    { &ngx_js_ssi_class_id,          ngx_js_ssi_members,          NULL },
    { &ngx_js_userid_class_id,       ngx_js_userid_members,       NULL },
    { &ngx_js_addition_class_id,     ngx_js_addition_members,     NULL },
    { &ngx_js_gunzip_class_id,       ngx_js_gunzip_members,       NULL },
    { &ngx_js_slice_class_id,        ngx_js_slice_members,        NULL },
    { &ngx_js_image_filter_class_id, ngx_js_image_filter_members, NULL },
    { &ngx_js_xslt_class_id,         ngx_js_xslt_members,         NULL },
    { &ngx_js_secure_link_class_id,  ngx_js_secure_link_members,  NULL },
    { &ngx_js_mp4_class_id,          ngx_js_mp4_members,          NULL },
    { &ngx_js_random_index_class_id, ngx_js_random_index_members, NULL },
    { &ngx_js_auth_request_class_id, ngx_js_auth_request_members, NULL },
    { &ngx_js_gzip_static_class_id,  ngx_js_gzip_static_members,  NULL },
    { &ngx_js_memcached_class_id,    ngx_js_memcached_members,    NULL },
    { &ngx_js_scgi_class_id,         ngx_js_scgi_members,         NULL },
    { &ngx_js_uwsgi_class_id,        ngx_js_uwsgi_members,        NULL },
    { &ngx_js_mirror_class_id,       ngx_js_mirror_members,       NULL },
    { &ngx_js_events_class_id,       ngx_js_events_members,       NULL },

    /* Stream */
    { &ngx_js_stream_ssl_class_id,    ngx_js_ssl_members,          NULL },
    { &ngx_js_stream_access_class_id, ngx_js_access_members,       NULL },
    { &ngx_js_stream_peer_class_id,   ngx_js_stream_peer_members,  NULL },
    { &ngx_js_stream_rr_peer_class_id, ngx_js_stream_rr_peer_members,
      ngx_js_stream_peer_prop_refine },
    { &ngx_js_stream_upstream_class_id, ngx_js_stream_upstream_members, NULL },

    /* Topology classes — follow-up #2 (close the describe() gaps) */
    { &ngx_js_upstream_class_id,        ngx_js_upstream_members,        NULL },
    { &ngx_js_socket_class_id,          ngx_js_socket_members,          NULL },
    { &ngx_js_http_listener_class_id,   ngx_js_http_listener_members,   NULL },
    { &ngx_js_cycle_class_id,           ngx_js_cycle_members,           NULL },
    { &ngx_js_stream_server_class_id,   ngx_js_stream_server_members,   NULL },
    { &ngx_js_stream_proxy_class_id,    ngx_js_stream_proxy_members,    NULL },
    { &ngx_js_stream_listener_class_id, ngx_js_stream_listener_members, NULL },
    { &ngx_js_snapshot_class_id,        ngx_js_snapshot_members,        NULL },

    { NULL, NULL, NULL }
};


/*
 * NginxSocketEntry — an element of nginx.cycle.sockets[] / nginx.http.sockets[].
 * A plain JS object (own data properties, no prototype getters), so NOTHING
 * described it before M2f: the tables only listed settable members, and the
 * read-only discovery pass only finds prototype getters.  describe() on a
 * socket entry returned an empty array.
 *
 * Every data member is a read-only fact about a listening socket.  The single
 * method resolves a server name against this listener.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

static const ngx_js_member_class_t  ngx_js_socket_entry_members[] = {
    { "address",      "string",   RO, 0,    WL,
      "Listening address, host:port form" },
    { "fd",           "number",   RO, 0,    WL, "Underlying socket descriptor" },
    { "type",         "string",   RO, 0,    WL, "'http' or 'stream'" },
    { "open",         "boolean",  RO, 0,    WL,
      "False after a {hard:true} removeListener closed the socket" },
    { "reuseport",    "boolean",  RO, 0,    WL, NULL },
    { "wildcard",     "boolean",  RO, 0,    WL, NULL },
    { "protocol",     "string",   RO, 0,    WL, NULL },
    { "jsCreated",    "boolean",  RO, 0,    WL,
      "True when this socket came from nginx.createSocket() + attach()" },
    { "jsHandle",     "number",   RO, 0,    WL,
      "createSocket() handle, or null for a socket from nginx.conf" },
    { "serverNames",  "string[]", RO, 0,    WL,
      "Every server_name routed by this listener" },
    { "serverByName", "function", RO, METH, WL,
      "Resolves a server name on this listener (case-insensitive) to its "
      "NginxServer / NginxStreamServer, or null; changes nothing",
      &ngx_js_sig_server_by_name },
    { NULL, NULL, 0, 0, 0, NULL, NULL }
};

#pragma GCC diagnostic pop

/* ------------------------------------------------------------------ *
 * Tag registry — for plain JS objects that are not class instances    *
 * (nginx.http).  describe() falls back to a hidden NGX_JS_DTAG_* tag   *
 * stamped by ngx_js_describe_tag() when the JSClassID lookup misses.   *
 */
#define NGX_JS_DTAG_PROP  "\xff" "ngxDescribeTag"

typedef struct {
    int                           tag;
    const ngx_js_member_class_t  *table;
    ngx_js_prop_refine_pt         refine;
} ngx_js_member_tag_registry_t;

static const ngx_js_member_tag_registry_t  ngx_js_member_tag_registry[] = {
    { NGX_JS_DTAG_HTTP,   ngx_js_http_members,         NULL },
    { NGX_JS_DTAG_SOCKET, ngx_js_socket_entry_members, NULL },
    { 0, NULL, NULL }
};


/* ------------------------------------------------------------------ *
 * Class catalog — the discovery root for nginx.describe() (no args).  *
 * Maps each classifiable COM class to a human name + its member table *
 * so tooling can enumerate "what exists" before drilling into a path. *
 */
static const struct {
    const char                   *name;
    const ngx_js_member_class_t  *table;
} ngx_js_class_catalog[] = {
    { "NginxHttp (nginx.http)",  ngx_js_http_members },
    { "NginxLocation",           ngx_js_loc_members },
    { "NginxServer",             ngx_js_server_members },
    { "NginxProxy",              ngx_js_proxy_members },
    { "NginxGzip",               ngx_js_gzip_members },
    { "NginxHeaders",            ngx_js_headers_members },
    { "NginxRewrite",            ngx_js_rewrite_members },
    { "NginxPeer",               ngx_js_peer_members },
    { "NginxRrPeer",             ngx_js_peer_members },
    { "NginxUpstream",           ngx_js_upstream_members },
    { "NginxSocket",             ngx_js_socket_members },
    { "NginxSocketEntry",        ngx_js_socket_entry_members },
    { "NginxHttpListener",       ngx_js_http_listener_members },
    { "NginxCycle",              ngx_js_cycle_members },
    { "NginxSSL",                ngx_js_ssl_members },
    { "NginxProxyCache",         ngx_js_proxy_cache_members },
    { "NginxAccess",             ngx_js_access_members },
    { "NginxAuth",               ngx_js_auth_members },
    { "NginxLimitReq",           ngx_js_limit_req_members },
    { "NginxLimitReqLimit",      ngx_js_limit_req_limit_members },
    { "NginxLimitConn",          ngx_js_limit_conn_members },
    { "NginxFastcgi",            ngx_js_fastcgi_members },
    { "NginxLog",                ngx_js_log_members },
    { "NginxRealip",             ngx_js_realip_members },
    { "NginxCharset",            ngx_js_charset_members },
    { "NginxSubFilter",          ngx_js_sub_filter_members },
    { "NginxAutoindex",          ngx_js_autoindex_members },
    { "NginxReferer",            ngx_js_referer_members },
    { "NginxDav",                ngx_js_dav_members },
    { "NginxSsi",                ngx_js_ssi_members },
    { "NginxUserid",             ngx_js_userid_members },
    { "NginxAddition",           ngx_js_addition_members },
    { "NginxGunzip",             ngx_js_gunzip_members },
    { "NginxSlice",              ngx_js_slice_members },
    { "NginxImageFilter",        ngx_js_image_filter_members },
    { "NginxXslt",               ngx_js_xslt_members },
    { "NginxSecureLink",         ngx_js_secure_link_members },
    { "NginxMp4",                ngx_js_mp4_members },
    { "NginxRandomIndex",        ngx_js_random_index_members },
    { "NginxAuthRequest",        ngx_js_auth_request_members },
    { "NginxGzipStatic",         ngx_js_gzip_static_members },
    { "NginxMemcached",          ngx_js_memcached_members },
    { "NginxScgi",               ngx_js_scgi_members },
    { "NginxUwsgi",              ngx_js_uwsgi_members },
    { "NginxMirror",             ngx_js_mirror_members },
    { "NginxEvents",             ngx_js_events_members },
    { "NginxSnapshot",           ngx_js_snapshot_members },
    { "NginxStreamServer",       ngx_js_stream_server_members },
    { "NginxStreamProxy",        ngx_js_stream_proxy_members },
    { "NginxStreamListener",     ngx_js_stream_listener_members },
    { "NginxStreamUpstream",     ngx_js_stream_upstream_members },
    { "NginxStreamPeer",         ngx_js_stream_peer_members },
    { "NginxStreamRrPeer",       ngx_js_stream_rr_peer_members },
    { "NginxStreamSSL",          ngx_js_ssl_members },
    { "NginxStreamAccess",       ngx_js_access_members },
    { NULL, NULL }
};


void
ngx_js_describe_tag(JSContext *ctx, JSValueConst obj, int tag)
{
    /* flags 0 → non-enumerable, non-writable, non-configurable (hidden). */
    JS_DefinePropertyValueStr(ctx, obj, NGX_JS_DTAG_PROP,
                              JS_NewInt32(ctx, tag), 0);
}


/* Resolve obj's tag (0 if absent / not tagged). */
static int
ngx_js_describe_obj_tag(JSContext *ctx, JSValueConst obj)
{
    JSValue  v;
    int32_t  tag = 0;

    v = JS_GetPropertyStr(ctx, obj, NGX_JS_DTAG_PROP);
    if (JS_IsNumber(v)) {
        JS_ToInt32(ctx, &tag, v);
    }
    JS_FreeValue(ctx, v);
    return (int) tag;
}


/*
 * Resolve obj to its classification table + refine hook.  Keys off the
 * JSClassID for class instances, then falls back to the hidden tag for plain
 * COM objects (nginx.http).  Returns NGX_OK with *table_out / *refine_out set,
 * or NGX_DECLINED if obj's class/tag is unregistered.
 */
static ngx_int_t
ngx_js_describe_resolve(JSContext *ctx, JSValueConst obj,
    const ngx_js_member_class_t **table_out, ngx_js_prop_refine_pt *refine_out)
{
    JSClassID                            cid;
    int                                  tag;
    const ngx_js_member_registry_t      *r;
    const ngx_js_member_tag_registry_t  *t;

    cid = JS_GetClassID(obj);

    for (r = ngx_js_member_registry; r->cid != NULL; r++) {
        if (*r->cid == cid) {
            *table_out  = r->table;
            *refine_out = r->refine;
            return NGX_OK;
        }
    }

    /* Plain object fallback: classify by hidden tag (e.g. nginx.http). */
    tag = ngx_js_describe_obj_tag(ctx, obj);
    if (tag != 0) {
        for (t = ngx_js_member_tag_registry; t->table != NULL; t++) {
            if (t->tag == tag) {
                *table_out  = t->table;
                *refine_out = t->refine;
                return NGX_OK;
            }
        }
    }

    return NGX_DECLINED;
}


/* ------------------------------------------------------------------ *
 * Descriptor construction                                             *
 * ------------------------------------------------------------------ */

static const char *
ngx_js_class_name(ngx_uint_t klass)
{
    switch (klass) {
    case NGX_JS_CLS_SAFE:         return "safe";
    case NGX_JS_CLS_GUARDED:      return "guarded";
    case NGX_JS_CLS_IRREVERSIBLE: return "irreversible";
    default:                      return "readonly";
    }
}


static const char *
ngx_js_propagation_name(ngx_uint_t prop)
{
    switch (prop) {
    case NGX_JS_PROP_ZONED_SHARED: return "zoned-shared";
    case NGX_JS_PROP_AUTO_SHARED:  return "auto-shared";
    default:                       return "worker-local";
    }
}


/*
 * M2b: attach the typed signature keys to a Descriptor.
 *
 *   params      [ { name, type, optional, mem } ]   (mem null unless a string)
 *   returns     string                              ("void" when none)
 *   returnsMem  string | null
 *   effects     [ string ]                          (split on ',')
 */
static void
ngx_js_describe_attach_sig(JSContext *ctx, JSValue d, const ngx_js_sig_t *sig)
{
    JSValue               arr, p, eff;
    const ngx_js_param_t *pm;
    const char           *s, *comma;
    uint32_t              i;

    arr = JS_NewArray(ctx);
    i = 0;

    if (sig->params != NULL) {
        for (pm = sig->params; pm->name != NULL; pm++) {
            p = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, p, "name", JS_NewString(ctx, pm->name));
            JS_SetPropertyStr(ctx, p, "type",
                              JS_NewString(ctx, pm->type ? pm->type : "any"));
            JS_SetPropertyStr(ctx, p, "optional", JS_NewBool(ctx, pm->optional));
            JS_SetPropertyStr(ctx, p, "mem",
                              pm->mem ? JS_NewString(ctx, pm->mem) : JS_NULL);
            JS_SetPropertyUint32(ctx, arr, i++, p);
        }
    }

    JS_SetPropertyStr(ctx, d, "params", arr);
    JS_SetPropertyStr(ctx, d, "returns",
                      JS_NewString(ctx, sig->returns ? sig->returns : "void"));
    JS_SetPropertyStr(ctx, d, "returnsMem",
                      sig->ret_mem ? JS_NewString(ctx, sig->ret_mem) : JS_NULL);

    eff = JS_NewArray(ctx);
    i = 0;

    for (s = sig->effects; s != NULL && *s != '\0'; ) {
        comma = strchr(s, ',');

        if (comma == NULL) {
            JS_SetPropertyUint32(ctx, eff, i++, JS_NewString(ctx, s));
            break;
        }

        JS_SetPropertyUint32(ctx, eff, i++,
                             JS_NewStringLen(ctx, s, (size_t) (comma - s)));
        s = comma + 1;
    }

    JS_SetPropertyStr(ctx, d, "effects", eff);
}


/* Build one Descriptor object for member m on obj (refine applied if present). */
static JSValue
ngx_js_describe_one(JSContext *ctx, JSValueConst obj,
    const ngx_js_member_class_t *m, ngx_js_prop_refine_pt refine)
{
    JSValue               d;
    ngx_js_propagation_e  prop;

    d = JS_NewObject(ctx);
    if (JS_IsException(d)) {
        return d;
    }

    prop = (ngx_js_propagation_e) m->propagation;
    if (refine != NULL) {
        prop = refine(ctx, obj, m);
    }

    JS_SetPropertyStr(ctx, d, "name", JS_NewString(ctx, m->name));
    JS_SetPropertyStr(ctx, d, "type",
                      JS_NewString(ctx, m->type ? m->type : "unknown"));

    /*
     * `type` alone conflates two different things under "function": a callable
     * method (addHook(), removeLocation()) and an assignable slot that happens
     * to hold a function (location.handler = fn).  The registry already knows
     * which is which — settable() is defined by it — so carry it out rather
     * than making every consumer re-derive it.  `callable` is also what makes a
     * signature MANDATORY: a callable member with no params[] is a hole in the
     * M3 call check, and t/js_com_describe.t enforces exactly that implication.
     */
    JS_SetPropertyStr(ctx, d, "callable",
                      JS_NewBool(ctx, (m->flags & NGX_JS_MF_METHOD) != 0));

    JS_SetPropertyStr(ctx, d, "access",
                      JS_NewString(ctx, m->klass == NGX_JS_CLS_READONLY
                                        ? "read-only" : "read-write"));
    JS_SetPropertyStr(ctx, d, "class",
                      JS_NewString(ctx, ngx_js_class_name(m->klass)));
    JS_SetPropertyStr(ctx, d, "reversible",
                      JS_NewBool(ctx, (m->flags & NGX_JS_MF_REVERSIBLE) != 0));
    JS_SetPropertyStr(ctx, d, "propagation",
                      JS_NewString(ctx, ngx_js_propagation_name(prop)));
    JS_SetPropertyStr(ctx, d, "requestScoped",
                      JS_NewBool(ctx,
                                 (m->flags & NGX_JS_MF_REQUEST_SCOPED) != 0));
    JS_SetPropertyStr(ctx, d, "note",
                      m->note ? JS_NewString(ctx, m->note) : JS_NULL);

    /*
     * M2b: typed signature.  Purely ADDITIVE — a member with no signature emits
     * exactly the original 8 keys, so existing consumers (and the .adoc spec)
     * are unaffected.  Typed members gain: params[], returns, returnsMem,
     * effects[].
     */
    if (m->sig != NULL) {
        ngx_js_describe_attach_sig(ctx, d, m->sig);
    }

    return d;
}


/* Is `name` already classified in `table`? */
static int
ngx_js_table_has(const ngx_js_member_class_t *table, const char *name)
{
    const ngx_js_member_class_t  *m;

    for (m = table; m->name != NULL; m++) {
        if (ngx_strcmp(m->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}


/*
 * Static type map for read-only getters (follow-up #3a).
 *
 * describe() never invokes a getter to learn its type — it must be
 * side-effect-free, and some COM getters (e.g. loc.proxy when the proxy module
 * is not configured for that location) dereference unconfigured module state
 * and crash if called outside a matching request.  So the type of a read-only
 * member is looked up here by name instead.  Entries are the verified,
 * cross-class-consistent getters operators inspect; anything not listed falls
 * back to type "getter" (still honest — it is a read-only accessor).
 */
static const struct {
    const char  *name;
    const char  *type;
} ngx_js_ro_types[] = {
    /* scalars */
    { "path",                    "string"   },
    { "pattern",                 "string"   },
    { "alias",                   "string"   },
    { "name",                    "string"   },
    { "address",                 "string"   },
    { "matchType",               "string"   },
    { "zone",                    "string"   },
    { "port",                    "number"   },
    { "fd",                      "number"   },
    { "typesHashMaxSize",        "number"   },
    { "internal",                "boolean"  },
    { "hasHandler",              "boolean"  },
    /* collections */
    { "names",                   "object[]" },
    { "locations",               "object[]" },
    { "peers",                   "object[]" },
    { "servers",                 "object[]" },
    { "upstreams",               "object[]" },
    { "sockets",                 "object[]" },
    { "tryFiles",                "object[]" },
    { "headerFilters",           "object[]" },
    { "bodyFilters",             "object[]" },
    /* single wrapper objects */
    { "ssl",                     "object"   },
    { "listener",                "object"   },
    { "largeClientHeaderBuffers","object"   },
    { NULL, NULL }
};

static const char *
ngx_js_ro_type_lookup(const char *name)
{
    ngx_uint_t  i;

    for (i = 0; ngx_js_ro_types[i].name != NULL; i++) {
        if (ngx_strcmp(ngx_js_ro_types[i].name, name) == 0) {
            return ngx_js_ro_types[i].type;
        }
    }
    return NULL;
}


/*
 * Build a Descriptor for a read-only getter `name`.  The getter is NOT invoked
 * (see ngx_js_ro_types above); the type comes from the static map, or "getter"
 * with an explanatory note when the member is not in the map.
 */
static JSValue
ngx_js_describe_readonly_one(JSContext *ctx, JSValueConst obj, const char *name)
{
    JSValue      d;
    const char  *type;

    (void) obj;

    d = JS_NewObject(ctx);
    if (JS_IsException(d)) {
        return d;
    }

    type = ngx_js_ro_type_lookup(name);

    JS_SetPropertyStr(ctx, d, "name",   JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, d, "type",   JS_NewString(ctx, type ? type : "getter"));
    /* This path fires only for a getter with no setter, so never a method.
     * Emitted anyway so `callable` is present on EVERY Descriptor — a consumer
     * testing d.callable must not get `undefined` from the read-only shape. */
    JS_SetPropertyStr(ctx, d, "callable", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, d, "access", JS_NewString(ctx, "read-only"));
    JS_SetPropertyStr(ctx, d, "class",  JS_NewString(ctx, "readonly"));
    JS_SetPropertyStr(ctx, d, "reversible",    JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, d, "propagation",
                      JS_NewString(ctx, "worker-local"));
    JS_SetPropertyStr(ctx, d, "requestScoped", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, d, "note",
                      type ? JS_NULL
                           : JS_NewString(ctx, "read-only accessor; value not "
                                               "pre-evaluated by describe()"));

    return d;
}


/*
 * Is `name` a read-only getter (getter, no setter) on obj's prototype?
 * Used so describe(obj, "path") works for read-only members too.
 */
static int
ngx_js_proto_is_readonly_getter(JSContext *ctx, JSValueConst obj,
    const char *name)
{
    JSValue               proto;
    JSAtom                atom;
    JSPropertyDescriptor  desc;
    int                   rc, ro;

    proto = JS_GetPrototype(ctx, obj);
    if (!JS_IsObject(proto)) {
        JS_FreeValue(ctx, proto);
        return 0;
    }

    atom = JS_NewAtom(ctx, name);
    rc = JS_GetOwnProperty(ctx, &desc, proto, atom);
    JS_FreeAtom(ctx, atom);

    ro = 0;
    if (rc > 0) {
        ro = (desc.flags & JS_PROP_GETSET)
             && JS_IsFunction(ctx, desc.getter)
             && !JS_IsFunction(ctx, desc.setter);
        JS_FreeValue(ctx, desc.value);
        JS_FreeValue(ctx, desc.getter);
        JS_FreeValue(ctx, desc.setter);
    }

    JS_FreeValue(ctx, proto);
    return ro;
}


/*
 * Append a read-only Descriptor for every getter-only property on obj's
 * prototype that the classification table does not already cover.  This makes
 * describe() a complete per-object reference (settable members from the table,
 * read-only members discovered here) without duplicating the getter lists.
 * Returns the next free array index.
 */
static uint32_t
ngx_js_describe_append_readonly(JSContext *ctx, JSValueConst obj,
    const ngx_js_member_class_t *table, JSValue arr, uint32_t i)
{
    JSValue          proto;
    JSPropertyEnum  *tab;
    uint32_t         len, k;

    proto = JS_GetPrototype(ctx, obj);
    if (!JS_IsObject(proto)) {
        JS_FreeValue(ctx, proto);
        return i;
    }

    if (JS_GetOwnPropertyNames(ctx, &tab, &len, proto, JS_GPN_STRING_MASK)
        != 0)
    {
        JS_FreeValue(ctx, proto);
        return i;
    }

    for (k = 0; k < len; k++) {
        JSPropertyDescriptor  desc;
        const char           *name;
        int                   ro;

        if (JS_GetOwnProperty(ctx, &desc, proto, tab[k].atom) <= 0) {
            continue;
        }

        ro = (desc.flags & JS_PROP_GETSET)
             && JS_IsFunction(ctx, desc.getter)
             && !JS_IsFunction(ctx, desc.setter);
        JS_FreeValue(ctx, desc.value);
        JS_FreeValue(ctx, desc.getter);
        JS_FreeValue(ctx, desc.setter);

        if (!ro) {
            continue;   /* settable (in table) or a method — skip */
        }

        name = JS_AtomToCString(ctx, tab[k].atom);
        if (name == NULL) {
            continue;
        }

        if (!ngx_js_table_has(table, name)) {
            JS_SetPropertyUint32(ctx, arr, i++,
                                 ngx_js_describe_readonly_one(ctx, obj, name));
        }

        JS_FreeCString(ctx, name);
    }

    JS_FreePropertyEnum(ctx, tab, len);
    JS_FreeValue(ctx, proto);
    return i;
}


/* nginx.describe(path) backend: array of Descriptors for every member. */
JSValue
ngx_js_describe_members(JSContext *ctx, JSValueConst obj)
{
    const ngx_js_member_class_t  *m, *table;
    ngx_js_prop_refine_pt         refine;
    JSValue                       arr, d;
    uint32_t                      i;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    if (ngx_js_describe_resolve(ctx, obj, &table, &refine) != NGX_OK) {
        return arr;   /* unregistered class/tag — empty, like settable() */
    }

    for (i = 0, m = table; m->name != NULL; m++, i++) {
        d = ngx_js_describe_one(ctx, obj, m, refine);
        if (JS_IsException(d)) {
            JS_FreeValue(ctx, arr);
            return d;
        }
        JS_SetPropertyUint32(ctx, arr, i, d);
    }

    /* Complete the reference: append read-only getters not in the table. */
    (void) ngx_js_describe_append_readonly(ctx, obj, table, arr, i);

    return arr;
}


/* nginx.describe(path, name) backend: one Descriptor, or JS_NULL. */
JSValue
ngx_js_describe_member(JSContext *ctx, JSValueConst obj, const char *name)
{
    const ngx_js_member_class_t  *m, *table;
    ngx_js_prop_refine_pt         refine;

    if (ngx_js_describe_resolve(ctx, obj, &table, &refine) != NGX_OK) {
        return JS_NULL;
    }

    for (m = table; m->name != NULL; m++) {
        if (ngx_strcmp(m->name, name) == 0) {
            return ngx_js_describe_one(ctx, obj, m, refine);
        }
    }

    /* Not a classified (settable) member — maybe a read-only getter. */
    if (ngx_js_proto_is_readonly_getter(ctx, obj, name)) {
        return ngx_js_describe_readonly_one(ctx, obj, name);
    }

    return JS_NULL;
}


/*
 * settable() backed by the describe() tables: the assignable members of obj's
 * classification table — every member that is NOT a callable method
 * (NGX_JS_MF_METHOD).  This is the single source of truth that lets settable()
 * cover every class describe() classifies (follow-up #2).  Returns an empty
 * array for objects with no classification table.
 */
JSValue
ngx_js_describe_settable_props(JSContext *ctx, JSValueConst obj)
{
    const ngx_js_member_class_t  *m, *table;
    ngx_js_prop_refine_pt         refine;
    JSValue                       arr;
    uint32_t                      i;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    if (ngx_js_describe_resolve(ctx, obj, &table, &refine) != NGX_OK) {
        return arr;
    }

    for (i = 0, m = table; m->name != NULL; m++) {
        if (m->flags & NGX_JS_MF_METHOD) {
            continue;   /* callable method — not an assignable property */
        }
        JS_SetPropertyUint32(ctx, arr, i++, JS_NewString(ctx, m->name));
    }

    return arr;
}


/*
 * nginx.describe() with no path — the discovery root.  Returns an array of
 * { class, members[] } for every classifiable COM class, so tooling can list
 * what exists before drilling into a specific path.
 */
JSValue
ngx_js_describe_catalog(JSContext *ctx)
{
    const ngx_js_member_class_t  *m;
    JSValue                       arr, entry, members;
    uint32_t                      i, j;

    arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        return arr;
    }

    for (i = 0; ngx_js_class_catalog[i].name != NULL; i++) {
        entry = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, entry, "class",
                          JS_NewString(ctx, ngx_js_class_catalog[i].name));

        members = JS_NewArray(ctx);
        for (j = 0, m = ngx_js_class_catalog[i].table; m->name != NULL; m++) {
            JS_SetPropertyUint32(ctx, members, j++, JS_NewString(ctx, m->name));
        }
        JS_SetPropertyStr(ctx, entry, "members", members);

        JS_SetPropertyUint32(ctx, arr, i, entry);
    }

    return arr;
}
