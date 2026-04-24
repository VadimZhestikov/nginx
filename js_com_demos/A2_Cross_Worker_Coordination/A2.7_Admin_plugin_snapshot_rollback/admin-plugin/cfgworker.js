// cfgworker.js — config-authority SharedWorker for the A2.7 admin plugin.
//
// Uses the onconnect/port pattern required by the nginx SharedWorker API:
//   - The SW maintains _ports[], one entry per connected nginx worker.
//   - Each worker sends CONNECT on activation; onconnect fires and the port
//     is added to _ports[] with a per-port onmessage handler.
//   - Data messages from workers are delivered as {data: <msg>} to the
//     per-port onmessage (so ev.data holds the actual message).
//   - Fan-out: iterate _ports and call p.postMessage(msg) for each.
//
// Protocol:
//   Worker → SW:
//     {type: 'apply',    snap: <snapshot>}   — apply snapshot
//     {type: 'rollback', snap: <snap>|null}  — rollback (null = base)
//     {type: 'get'}                          — request current state
//
//   SW → all workers (fan-out):
//     {type: 'apply',    snap: <snapshot>}
//     {type: 'rollback', snap: <snap>|null}
//
//   SW → requesting worker only (reply to 'get'):
//     {type: 'sync',     snap: <snapshot>|null}

var _desired = null;
var _ports   = [];

onconnect = function (e) {
    var port = e.ports[0];
    _ports.push(port);

    port.onmessage = function (ev) {
        var type = ev.data.type;

        if (type === 'apply' || type === 'rollback') {
            _desired = (ev.data.snap !== undefined) ? ev.data.snap : null;
            /* Fan out the same message to ALL connected workers. */
            _ports.forEach(function (p) { p.postMessage(ev.data); });

        } else if (type === 'get') {
            /* Reply with the current desired state to the requesting worker. */
            port.postMessage({ type: 'sync', snap: _desired });
        }
    };
};
