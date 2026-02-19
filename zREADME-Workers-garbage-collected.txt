❯ just a question: are Workers garbage collected?

● Yes. Worker JS objects are subject to QuickJS's reference-counting GC. When the last reference to a Worker object drops to zero the finalizer fires, which sends a terminate signal to the pthread
  and closes the communication pipe.

  In practice the closure pattern protects you:

  var w = new Worker('foo.js');
  w.onmessage = function(e) {   // closure captures w
      w.terminate();
      resolve(e.data);
  };

  w.onmessage is a property of w, and the closure captures w, creating a cycle. QuickJS's cycle collector keeps both alive until the Promise resolves and neither is reachable. So in the normal await
  new Promise(...) pattern there is no premature collection.

  Where it does bite you is exactly the bug we fixed in ngx_js_wt_sw_t: a Worker thread calling new SharedWorker(url) where var sw is a local in onmessage. Once onmessage returns the frame is gone,
  sw's refcount hits zero, the finalizer closes worker_fd, and the thread's poll loop never sees it. The fix was sw->js_obj = JS_DupValue(ctx, obj) — a C-side strong reference that keeps the JS
  object alive until the thread explicitly calls JS_FreeValue(ctx, sw->js_obj).

  The same risk exists any time you store a Worker reference only on the C side (e.g. in a list or struct) without a corresponding JS_DupValue. As long as you hold it in a live JS variable or
  property the GC is fine.

✻ Baked for 34s
