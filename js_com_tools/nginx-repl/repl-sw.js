'use strict';
/*
 * repl-sw.js — SharedWorker broker for the nginx JS REPL.
 *
 * Each nginx worker registers itself here on startup:
 *   { type: 'repl-register', workerId: <N> }
 * with a MessageChannel port2 transferred.
 *
 * The REPL client (repl-client.js) connects via HTTP, hijacks the connection,
 * and communicates through the worker that handled the request.  That worker
 * relays EVAL commands to the target worker through this broker.
 *
 * Message types:
 *
 *   Worker → SW (via self.onmessage):
 *     { type: 'repl-register', workerId }  + transferred MessagePort
 *
 *   Client-facing worker → SW (via registered port):
 *     { type: 'repl-eval', token, line, targetWorker, replyWorker }
 *
 *   Target worker → SW (via registered port):
 *     { type: 'repl-result', token, result, replyWorker }
 */

/* Map of workerId → { port: MessagePort } */
const workers = new Map();


self.onmessage = function (e) {
    const msg = e.data;
    if (!msg || typeof msg.type !== 'string') { return; }

    if (msg.type === 'repl-register') {
        const port = e.ports && e.ports[0];
        if (!port) { return; }

        workers.set(msg.workerId, { port });

        port.onmessage = function (pe) {
            const pm = pe.data;
            if (!pm || typeof pm.type !== 'string') { return; }

            if (pm.type === 'repl-eval') {
                /* Route eval request to the target worker */
                const target = workers.get(pm.targetWorker);
                if (target) {
                    target.port.postMessage({
                        type:        'repl-eval',
                        token:       pm.token,
                        line:        pm.line,
                        replyWorker: pm.replyWorker,
                    });
                }

            } else if (pm.type === 'repl-result') {
                /* Route result back to the requesting worker */
                const reply = workers.get(pm.replyWorker);
                if (reply) {
                    reply.port.postMessage({
                        type:   'repl-result',
                        token:  pm.token,
                        result: pm.result,
                    });
                }
            }
        };

        port.start();
    }
};
