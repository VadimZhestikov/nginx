// cfgworker.js — config-authority SharedWorker for admin snapshot/rollback.
//
// Uses the onconnect/port pattern required by the nginx SharedWorker API.
// See admin-plugin/cfgworker.js for protocol description.

var _desired = null;
var _ports   = [];

onconnect = function (e) {
    var port = e.ports[0];
    _ports.push(port);

    port.onmessage = function (ev) {
        var type = ev.data.type;

        if (type === 'apply' || type === 'rollback') {
            _desired = (ev.data.snap !== undefined) ? ev.data.snap : null;
            /* Skip the sender — it already applied the change locally. */
            _ports.forEach(function (p) {
                if (p !== port) { p.postMessage(ev.data); }
            });

        } else if (type === 'get') {
            port.postMessage({ type: 'sync', snap: _desired });
        }
    };
};
