(function () {
  var s = nginx.http.servers[0];
  var loc = s.locations.find(function (l) { return l.path === '/'; });
  loc.handler = function (r) { r.respond(200, {'content-type': 'text/plain'}, 'ok\n'); };
  mirror.attach(s, loc, {
    onRequestHeaders: function (ev) {
      ev.flow.c = ev.table.incr('maxim:count');
      ev.flow.t = ev.header('x-tenant') || '-';
    },
    onResponseHeaders: function (ev) {
      ev.setResponseHeader('x-count', String(ev.flow.c));
      ev.setResponseHeader('x-tenant-seen', ev.flow.t);
    }
  });
})();
