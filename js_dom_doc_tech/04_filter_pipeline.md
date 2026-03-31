# 04 — Body Filter Pipeline (P5 / P15)

## Purpose

Body filters transform the response body between the content handler and the
client.  JS_Pilgrim supports five execution modes, covering synchronous
transforms, async Promises, and fully streaming async generators.

Multiple filters on the same location (P15) compose automatically: output of
filter N becomes input to filter N+1.

---

## Installation

```javascript
// Whole-body synchronous
loc.addBodyFilter(function(req, body) {
    return body.toUpperCase();
});

// Whole-body async (Promise)
loc.addBodyFilter(async function(req, body) {
    return await compress(body);
});

// Streaming async generator
loc.addBodyFilter(async function*(body, req) {
    for await (const chunk of body) {
        yield transform(chunk);
    }
});
```

`addBodyFilter` appends the function to `loc->filter_fns[]` (up to 8 per
location).  The mode is inferred from the function type at installation time
by inspecting the `JS_IsGeneratorFunction` flag.

---

## Five Execution Modes

### WB_SYNC (whole-body synchronous)
- The response body is fully buffered in `rctx->wb_body`.
- The filter function is called synchronously with `(req, body_string)`.
- Return value replaces the body.
- Implemented in `ngx_js_run_wbsync_filter`.

### WB_ASYNC (whole-body async)
- Same buffering as WB_SYNC.
- The filter returns a Promise; the request is suspended until resolution.
- `ngx_js_bf_pending_t` tracks the pending Promise and the request.
- Resume path: `ngx_js_bf_async_check` is called from `ngx_js_async_check`.

### STREAM_SYNC (streaming synchronous)
- The filter is called for each incoming chunk: `fn(req, chunk, flags)`.
- The filter calls `req.sendBuffer(outChunk)` to enqueue output.
- `rctx->stream_out` accumulates the output chain.
- No suspension; the function must return synchronously.

### STREAM_ASYNC (streaming async)
- Same as STREAM_SYNC but the filter returns a Promise per chunk.
- Suspension until each Promise resolves.
- `ngx_js_sf_pending_t` tracks the pending state.

### GENERATOR (async generator function)
- The body upstream is exposed as an async iterable passed to the generator.
- The generator yields chunks downstream.
- Most powerful mode: can buffer, split, merge, or reorder chunks.
- Implemented via QuickJS's `JS_IteratorNext` / `JS_IteratorReturn`.

---

## Multiple Filters (P15)

When a location has N filters `[f0, f1, …, fN-1]`, they run in sequence:

```
nginx response body
  → [f0 buffering / generator]
  → [f1 buffering / generator]
  → …
  → downstream nginx filter chain
```

The C dispatcher iterates `rctx->filter_idx` (0 to N-1).  Each filter either
resolves synchronously (continues immediately) or suspends with a `bf_pending`
or `sf_pending`.  When a pending filter resumes, `resume_idx` in the pending
struct identifies which filter to advance.

Mixed modes are allowed: filter 0 can be a whole-body async filter and filter 1
can be a streaming sync filter.

---

## Key State Variables in ngx_js_req_ctx_t

```c
ngx_chain_t    *body_bufs;       /* accumulated response body (WB modes) */
ngx_str_t       wb_body;         /* whole-body string passed to the filter fn */
ngx_chain_t    *stream_out;      /* sendBuffer accumulator (streaming modes) */
ngx_chain_t   **stream_out_last; /* tail pointer */
ngx_uint_t      filter_idx;      /* index of the currently executing filter */
JSValue         gen_obj;         /* current generator JS object (GENERATOR mode) */
ngx_chain_t    *gen_out;         /* output chain from yielded values */
```

---

## Whole-Body Buffer Accumulation

For WB_SYNC and WB_ASYNC modes, the body must be fully received before the
filter is called.  `ngx_js_body_filter` (the nginx body filter hook) appends
each `ngx_chain_t` to `rctx->body_bufs`.  When it sees `NGX_HTTP_LAST_CHUNK`
in the flags, it converts the chain to a flat `ngx_str_t` (`rctx->wb_body`) and
calls the filter function.

Memory: the flat string is allocated from the request pool (`ngx_pnalloc`), so it
is freed automatically on request cleanup.

---

## Upstream Filters (P14)

`location.addUpstreamFilter(fn)` and `location.addUpstreamRequestFilter(fn)` work
similarly but intercept data flowing between nginx and the upstream backend:

- **Upstream request filter**: transforms what JS sends to the upstream (req body).
- **Upstream response filter**: transforms what the upstream sends back (res body).

Implementation is in `ngx_js_com_http.c`.  The upstream body is passed through
the same WB/streaming/generator dispatch as P5 filters.  The difference is the
buffer source (upstream response body chain) and the injection point (nginx
upstream body filter chain, not the response body filter chain).

---

## ngx_js_run_chain

`ngx_js_run_chain(r, chain, flags)` is the final step that sends a transformed
`ngx_chain_t` into the nginx downstream filter pipeline.  It:

1. Calls `ngx_http_output_filter(r, chain)` (the nginx chain).
2. Handles `NGX_AGAIN` (partial write) by deferring to a write event handler.
3. Updates `rctx->stream_out` tail after each partial write.

This function is reused by both the async-resume path and the synchronous
streaming path.
