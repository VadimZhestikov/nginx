// M1's "count + tag" policy, identical in behaviour to
// ../maxim_m1/maxim_mirror_app.js, but compiled at load via AOT-A.
//
// nginx.jitCompile(fn[, opts]) runs the SAME path COMCON C5 uses for an
// admitted fragment -- js_jit_compile_all + drain + install -- on host JS,
// synchronously, in the MASTER at config load. That is the only place it can
// work: the gcc worker is a pthread and does not survive fork(), so every
// enqueue path is inert in a worker. What the master installs is inherited by
// every worker through COW.
//
// WHAT IS COMPILED, and why it is not just the three handlers below: the
// per-request work is mostly mirror's own dispatcher, which is the closure
// `attach` passes to location.addHook. jitCompile walks a function's whole
// nested tree, so compiling mirror.attach reaches that dispatcher. Compiling
// only the app's leaf handlers would leave the hot path interpreted and
// understate the arm.
//
// Each call returns a REPORT; `installed` is the only field meaning compiled
// code exists. They are summed and logged so this arm cannot post a number
// while having silently compiled nothing -- the exact failure this harness was
// built to prevent.

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

  // Compile at load. Roots chosen to cover the per-request path:
  // mirror's dispatcher (inside attach), the table methods the policy calls,
  // and the policy's own handlers.
  var roots = [
    ['mirror.attach',   mirror.attach],
    ['mirror.table',    mirror.table && mirror.table.incr],
    ['handler',         handler],
    ['onRequestHeaders', onRequestHeaders],
    ['onResponseHeaders', onResponseHeaders]
  ];

  var total = { walked: 0, attempted: 0, installed: 0, skipped: 0, ms: 0 };
  var per = [];

  roots.forEach(function (pair) {
    if (typeof pair[1] !== 'function') { return; }
    var rep = nginx.jitCompile(pair[1]);
    total.walked    += rep.walked;
    total.attempted += rep.attempted;
    total.installed += rep.installed;
    total.skipped   += rep.skipped;
    total.ms        += rep.ms;
    per.push(pair[0] + '=' + rep.installed + '/' + rep.walked);
  });

  nginx.log(6, 'JITAOT total ' + JSON.stringify(total) + ' per ' + per.join(' '));
})();
