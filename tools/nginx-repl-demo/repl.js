(function () {

    var relay    = new SharedWorker(nginx.cycle.prefix + 'repl-relay.js');
    var _pending = {};   /* token → function(resultStr) */

    function _fmt(token, r) {
        if (r.status === 'ok') {
            var val = (r.value !== undefined && r.value !== null)
                      ? String(r.value) : '';
            return token + ' OK ' + val + '\n';
        }
        if (r.status === 'incomplete') { return token + ' INCOMPLETE\n'; }
        var out = token + ' ERR ' + r.message + '\n';
        if (r.stack) {
            String(r.stack).split('\n').forEach(function (l) {
                if (l) { out += token + ' STACK ' + l + '\n'; }
            });
            out += token + ' STACK_END\n';
        }
        return out;
    }

    /*
     * The relay.onmessage handler must be set inside nginx.broadcast() so
     * that it is stored in each worker's own worker_slots[ngx_worker].
     * Setting it only at init_conf time (in the master, where ngx_worker=0)
     * would leave workers 1-N without a handler, silently dropping messages.
     *
     * We also defer the register postMessage to setTimeout(0) so that
     * ngx_js_sw_activate() runs after ngx_event_process_init() has
     * initialised free_connections (it is NULL during ngx_js_init_process).
     */
    nginx.broadcast(function () {
        relay.onmessage = function (e) {
            var msg = e.data;
            if (!msg) { return; }

            if (msg.type === 'eval') {
                /* We are the target worker */
                relay.postMessage({
                    type:        'result',
                    token:       msg.token,
                    result:      _fmt(msg.token, nginx.repl.eval(msg.line)),
                    replyWorker: msg.replyWorker,
                });
            } else if (msg.type === 'result') {
                /* We are the gateway worker */
                var cb = _pending[msg.token];
                if (cb) { delete _pending[msg.token]; cb(msg.result); }
            }
        };

        nginx.setTimeout(0).then(function () {
            relay.postMessage({ type: 'register', workerId: nginx.workerIdx });
        });
    });

    /* ------------------------------------------------------------------ */
    var loc = nginx.http.servers[0].locations.find(function (l) {
        return l.path === '/repl/';
    });

    loc.handler = function (req) {
        var w      = req.queryParams.w;
        var target = (w !== undefined && w !== '')
                     ? parseInt(w, 10) : nginx.workerIdx;

        var fd = req.hijack();
        nginx.repl._writeFd(fd,
            'HTTP/1.1 101 Switching Protocols\r\n' +
            'Connection: Upgrade\r\n' +
            'Upgrade: nginx-repl\r\n' +
            '\r\n');

        nginx.repl.attach(fd, 2, 6);

        nginx.repl.listen(fd, function (line) {
            var sp1 = line.indexOf(' ');
            if (sp1 < 0) { return; }
            var token = line.slice(0, sp1);
            var rest  = line.slice(sp1 + 1);
            var sp2   = rest.indexOf(' ');
            var cmd   = sp2 < 0 ? rest : rest.slice(0, sp2);
            var arg   = sp2 < 0 ? ''   : rest.slice(sp2 + 1);

            if (cmd === 'EVAL') {
                if (target === nginx.workerIdx) {
                    nginx.repl._writeFd(fd, _fmt(token, nginx.repl.eval(arg)));
                } else {
                    _pending[token] = function (result) {
                        nginx.repl._writeFd(fd, result);
                    };
                    relay.postMessage({
                        type:         'eval',
                        token:        token,
                        line:         arg,
                        targetWorker: target,
                        replyWorker:  nginx.workerIdx,
                    });
                }
            } else if (cmd === 'DETACH') {
                nginx.repl.detach();
                nginx.repl._writeFd(fd, token + ' OK\n');
            }
        });
    };

})();
