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
      "Rebuilds live location BST; reverse with removeLocation" },
    { "removeLocation",           "function",GRD, REV|METH,     WL,
      "Tombstone (reversible via restoreLocation); in-flight 404s not undone; "
      "{hard:true} for irreversible splice" },
    { "restoreLocation",          "function",GRD, REV|METH,     WL,
      "Clears a removeLocation tombstone; brings the route back" },
    { "clearHandler",             "function",SAFE, REV|METH,    WL,
      "Restores the location's original (pre-JS) handler" },
    { NULL, NULL, 0, 0, 0, NULL }
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
      "Copy-on-write append into a fresh sub-pool" },
    { "removeHeader",    "function", SAFE, REV|RQS|METH, WL, NULL },
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
    { "setCiphers",          "function",GRD, REV|METH, WL, "Mutates live SSL_CTX" },
    { "setProtocols",        "function",GRD, REV|METH, WL, "Mutates live SSL_CTX" },
    { "setCertificate",      "function",GRD, REV|METH, WL,
      "Hot-swaps the live certificate + key" },
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
      "Sets source + destination charset" },
    { NULL, NULL, 0, 0, 0, NULL }
};

static const ngx_js_member_class_t  ngx_js_sub_filter_members[] = {
    { "once",         "boolean", SAFE, REV, WL, NULL },
    { "lastModified", "boolean", SAFE, REV, WL, NULL },
    { "setPairs",     "function",SAFE, REV|METH, WL, "Replaces substitution pairs" },
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
      "for the process lifetime (irreversible)" },
    { "removeServer",    "function", GRD, REV|METH, WL,
      "Tombstone (reversible via restoreServer); {hard:true} for irreversible "
      "splice" },
    { "restoreServer",   "function", GRD, REV|METH, WL,
      "Clears a removeServer tombstone; brings the virtual server back" },
    { "attach",          "function", IRR, METH,   WL,
      "Binds a createSocket() fd into cycle->listening; a committed resource, "
      "never reclaimed at runtime (irreversible)" },
    { "removeListener",  "function", GRD, REV|METH, WL,
      "Soft pause (reversible via restoreListener); {hard:true} closes the "
      "socket — connections refused, port freed (irreversible)" },
    { "restoreListener", "function", GRD, REV|METH, WL,
      "Re-arms a soft-paused listener; returns false after a {hard:true} close" },
    { "addHook",         "function", GRD, REV|METH, WL,
      "Registers a global access-phase hook; reverse by clearing it" },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxUpstream (HTTP) — live RR peer topology; zoned-shared like the stream
 * upstream's (cross-worker iff the upstream is zone-backed). */
static const ngx_js_member_class_t  ngx_js_upstream_members[] = {
    { "addPeer",    "function", GRD, REV|METH, ZS,
      "Adds a live RR peer (under rr_peers wlock when zone-backed)" },
    { "removePeer", "function", GRD, REV|METH, ZS, "Reverse with addPeer()" },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxSocket — createSocket() handle. */
static const ngx_js_member_class_t  ngx_js_socket_members[] = {
    { "close",     "function", GRD, REV|METH, WL,
      "Closes the fd and unregisters; recreate with nginx.createSocket()" },
    { "broadcast", "function", GRD, REV|METH, WL,
      "Distributes the fd to all workers via the manager thread" },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxHttpListener — nginx.http.attach() handle.  addServer / addVirtualServer
 * activate the listener (push into cycle->listening / rebuild virtual_names),
 * a committed resource → irreversible; the hook/filter registrations are
 * guarded and reversible. */
static const ngx_js_member_class_t  ngx_js_http_listener_members[] = {
    { "addServer",        "function", IRR, METH,   WL,
      "Activates the listener (cycle->listening); committed (irreversible)" },
    { "addVirtualServer", "function", IRR, METH,   WL,
      "Rebuilds virtual_names host routing; committed (irreversible)" },
    { "on",               "function", GRD, REV|METH, WL,
      "Registers an accept hook" },
    { "addL4Filter",      "function", GRD, REV|METH, WL,
      "Registers a raw inbound TCP filter" },
    { "addL4SendFilter",  "function", GRD, REV|METH, WL,
      "Registers a raw outbound TCP filter" },
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
      "Activates the listener (cycle->listening); committed (irreversible)" },
    { "addVirtualServer", "function", IRR, METH, WL,
      "Rebuilds stream virtual_names routing; committed (irreversible)" },
    { NULL, NULL, 0, 0, 0, NULL }
};

/* NginxSnapshot — restore re-applies captured property values. */
static const ngx_js_member_class_t  ngx_js_snapshot_members[] = {
    { "restore", "function", GRD, REV|METH, WL,
      "Re-applies captured property values; reversible by re-snapshotting" },
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
      "Takes effect only after rebuildVhostDispatch()" },
    { "addLocation",            "function",GRD, REV|METH, WL,
      "Rebuilds live BST; reverse with removeLocation" },
    { "removeLocation",         "function",GRD, REV|METH, WL,
      "Tombstone (reversible via restoreLocation); in-flight 404s not undone; "
      "{hard:true} for irreversible splice" },
    { "restoreLocation",        "function",GRD, REV|METH, WL,
      "Clears a removeLocation tombstone" },
    { "clone",                  "function",SAFE, REV|METH, WL,
      "Produces a detached config object; no live effect until added" },
    { "addHook",                "function",GRD, REV|METH, WL, NULL },
    { "on",                     "function",GRD, REV|METH, WL, NULL },
    { "addL4Filter",            "function",GRD, REV|METH, WL, NULL },
    { "addL4SendFilter",        "function",GRD, REV|METH, WL, NULL },
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
    { "addPeer",    "function", GRD, REV|METH, ZS, "Adds a live RR peer" },
    { "removePeer", "function", GRD, REV|METH, ZS, "Reverse with addPeer()" },
    { NULL, NULL, 0, 0, 0, NULL }
};


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
    { NGX_JS_DTAG_HTTP, ngx_js_http_members, NULL },
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
 * Build a Descriptor for a read-only getter `name`.
 *
 * The getter is deliberately NOT invoked: describe() must have no side effects,
 * and some COM getters (e.g. loc.proxy when the proxy module is not configured
 * for that location) dereference unconfigured module state and would crash if
 * called outside a matching request.  So the type is reported as "getter"
 * rather than the runtime value's typeof.  (Static read-only types are a
 * possible future refinement — see follow-up #3.)
 */
static JSValue
ngx_js_describe_readonly_one(JSContext *ctx, JSValueConst obj, const char *name)
{
    JSValue  d;

    (void) obj;

    d = JS_NewObject(ctx);
    if (JS_IsException(d)) {
        return d;
    }

    JS_SetPropertyStr(ctx, d, "name",          JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, d, "type",          JS_NewString(ctx, "getter"));
    JS_SetPropertyStr(ctx, d, "access",        JS_NewString(ctx, "read-only"));
    JS_SetPropertyStr(ctx, d, "class",         JS_NewString(ctx, "readonly"));
    JS_SetPropertyStr(ctx, d, "reversible",    JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, d, "propagation",
                      JS_NewString(ctx, "worker-local"));
    JS_SetPropertyStr(ctx, d, "requestScoped", JS_NewBool(ctx, 0));
    JS_SetPropertyStr(ctx, d, "note",
                      JS_NewString(ctx, "read-only accessor; value not "
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
