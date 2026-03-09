
/*
 * Copyright (C) nginx JS contributors
 *
 * Minimal public interface to ngx_http_try_files_module internals,
 * exposing only the types needed by the JS COM layer.
 */

#ifndef _NGX_HTTP_TRY_FILES_MODULE_H_INCLUDED_
#define _NGX_HTTP_TRY_FILES_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


/*
 * Each try_files argument is one entry.  The array is terminated by a
 * sentinel entry where lengths == NULL && name.len == 0.
 *
 * For static entries (lengths == NULL): name.len includes a trailing '\0',
 * so the printable length is name.len - 1.
 * For variable-containing entries (lengths != NULL): name.len is exact.
 */
typedef struct {
    ngx_array_t  *lengths;
    ngx_array_t  *values;
    ngx_str_t     name;

    unsigned      code:10;
    unsigned      test_dir:1;
} ngx_http_try_file_t;

typedef struct {
    ngx_http_try_file_t  *try_files;  /* NULL-sentinel-terminated array */
} ngx_http_try_files_loc_conf_t;


extern ngx_module_t  ngx_http_try_files_module;


#endif /* _NGX_HTTP_TRY_FILES_MODULE_H_INCLUDED_ */
