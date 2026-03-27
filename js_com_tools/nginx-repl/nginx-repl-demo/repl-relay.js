/*
 * repl-relay.js — SharedWorker that routes REPL eval requests between workers.
 *
 * Workers register on startup:
 *   sw.postMessage({ type: 'register', workerId: N })
 *
 * Gateway sends eval to target:
 *   sw.postMessage({ type: 'eval', token, line, targetWorker, replyWorker })
 *
 * Target evaluates, sends result back:
 *   sw.postMessage({ type: 'result', token, result, replyWorker })
 *
 * Pending evals for unregistered workers are queued and flushed on registration.
 */

var workerPorts = {};   /* workerId → port */
var queued      = {};   /* workerId → [pending eval messages] */

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (pe) {
        var msg = pe.data;
        if (!msg) { return; }

        if (msg.type === 'register') {
            workerPorts[msg.workerId] = port;
            /* flush any evals queued before this worker registered */
            var pending = queued[msg.workerId];
            if (pending) {
                delete queued[msg.workerId];
                for (var i = 0; i < pending.length; i++) {
                    port.postMessage(pending[i]);
                }
            }

        } else if (msg.type === 'eval') {
            var tp = workerPorts[msg.targetWorker];
            if (tp) {
                tp.postMessage({
                    type:        'eval',
                    token:       msg.token,
                    line:        msg.line,
                    replyWorker: msg.replyWorker,
                });
            } else {
                /* target not yet registered — queue */
                if (!queued[msg.targetWorker]) {
                    queued[msg.targetWorker] = [];
                }
                queued[msg.targetWorker].push({
                    type:        'eval',
                    token:       msg.token,
                    line:        msg.line,
                    replyWorker: msg.replyWorker,
                });
            }

        } else if (msg.type === 'result') {
            var rp = workerPorts[msg.replyWorker];
            if (rp) {
                rp.postMessage({
                    type:   'result',
                    token:  msg.token,
                    result: msg.result,
                });
            }
        }
    };
};
