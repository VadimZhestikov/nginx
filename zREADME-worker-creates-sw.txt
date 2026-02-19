● The feature is complete. Here's a summary of what was done:

  Root cause of hang: After the JS Worker's onmessage function returned, the local var sw variable went out of scope. QuickJS GC immediately collected the SharedWorker JS object, triggering
  ngx_js_wt_sw_finalizer which unlinked the entry from tctx->sw_list and closed the channel fd. The Worker thread then entered poll(nfds=1) with no SW fd to watch — the reply from the SW thread never
   arrived.

  Fix: Added JSValue js_obj to ngx_js_wt_sw_t. In the constructor, sw->js_obj = JS_DupValue(ctx, obj) keeps refcount ≥ 1, preventing GC. In done:, explicit JS_FreeValue(ctx, sw_iter->js_obj) triggers
   the finalizer (which frees the struct); ngx_free(tctx) is deferred after JS_FreeRuntime so finalizers can safely dereference sw->list = &tctx->sw_list.

  Secondary fix: The previous done: section manually freed ngx_js_wt_sw_t structs AND called ngx_free(tctx) before JS_FreeRuntime, causing double-free (SIGABRT) and use-after-free when finalizers
  ran.

✻ Crunched for 36m 1s
