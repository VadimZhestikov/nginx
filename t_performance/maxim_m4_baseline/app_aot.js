// M1's "count + tag" policy, identical in behaviour to
// ../maxim_m1/maxim_mirror_app.js, but with the functions hoisted to named
// references so each can be handed to nginx.jitCompile().
//
// nginx.jitCompile() runs the SAME path COMCON C5 uses for an admitted
// fragment -- js_jit_compile_all + js_jit_drain + js_jit_install_results --
// synchronously, at load. Host JS is not compiled otherwise: C5 covers
// comcon.include fragments only, and the engine's automatic path merely
// enqueues without anything ever draining it.
//
// MEASURED: this changes nothing. jitCompile reports true for all three, but
// true only means "eligible" -- and zero C is generated. The JIT's GCC worker
// thread does not survive nginx's fork, so it is inert in every worker (see
// the nginx.jitCompile comment in src/js/ngx_js_com.c). Kept as the
// reproduction: if per-worker JIT activation lands, this arm should diverge
// from the plain jit arm.

(function () {
  var s = nginx.http.servers[0];
  var loc = s.locations.find(function (l) { return l.path === '/'; });

  function handler(r) {
    r.respond(200, {'content-type': 'text/plain'}, 'ok\n');
  }

  function onRequestHeaders(ev) {
    ev.flow.c = ev.table.incr('maxim:count');
    ev.flow.t = ev.header('x-tenant') || '-';
  }

  function onResponseHeaders(ev) {
    ev.setResponseHeader('x-count', String(ev.flow.c));
    ev.setResponseHeader('x-tenant-seen', ev.flow.t);
  }

  loc.handler = handler;

  mirror.attach(s, loc, {
    onRequestHeaders:  onRequestHeaders,
    onResponseHeaders: onResponseHeaders
  });

  // Compile at load. Report per-function so a silent no-op cannot masquerade
  // as a fast policy.
  var res = {
    handler:  nginx.jitCompile(handler),
    onReq:    nginx.jitCompile(onRequestHeaders),
    onResp:   nginx.jitCompile(onResponseHeaders)
  };

  nginx.log(6, 'JITAOT ' + JSON.stringify(res));
})();
