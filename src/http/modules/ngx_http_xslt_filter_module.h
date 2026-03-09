
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_xslt_filter_module internals,
 * exposing only the types needed by the JS COM layer.
 *
 * xsltStylesheetPtr and xmlDtdPtr are opaque here — we use void * to
 * avoid a hard dependency on libxslt/libxml2 headers in the COM layer.
 */

#ifndef _NGX_HTTP_XSLT_FILTER_MODULE_H_INCLUDED_
#define _NGX_HTTP_XSLT_FILTER_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
    u_char                    *name;
    ngx_http_complex_value_t   value;
    ngx_uint_t                 quote;        /* unsigned  quote:1; */
} ngx_http_xslt_param_t;


typedef struct {
    void          *stylesheet;  /* xsltStylesheetPtr — opaque */
    ngx_array_t    params;      /* ngx_http_xslt_param_t */
} ngx_http_xslt_sheet_t;


typedef struct {
    void          *dtd;         /* xmlDtdPtr — opaque */
    ngx_array_t    sheets;      /* ngx_http_xslt_sheet_t */
    ngx_hash_t     types;
    ngx_array_t   *types_keys;
    ngx_array_t   *params;      /* ngx_http_xslt_param_t */
    ngx_flag_t     last_modified;
} ngx_http_xslt_filter_loc_conf_t;


extern ngx_module_t  ngx_http_xslt_filter_module;


#endif /* _NGX_HTTP_XSLT_FILTER_MODULE_H_INCLUDED_ */
