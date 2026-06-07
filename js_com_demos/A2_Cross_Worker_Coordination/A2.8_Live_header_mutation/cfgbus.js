// cfgbus.js — Config-header broadcast bus for A2.8 demo
//
// A minimal SharedWorker that fans header mutations out to every nginx worker.
// The SW thread runs in the master process; each worker holds one port.
//
// Protocol (worker → SW):
//   {type:'addHeader',    key, value, always}  — add one header
//   {type:'removeHeader', key}                 — remove by key (case-insensitive)
//   {type:'clearHeaders'}                      — wipe all entries
//
// Protocol (SW → worker):
//   same message shapes, fanned out to all OTHER ports
//   {type:'setState', headers:[{key,value,always},...]}  — on connect catch-up

var _ports  = [];
var _current = [];   // canonical current state maintained by the SW

function applyToState(msg) {
    if (msg.type === 'addHeader') {
        _current.push({key: msg.key, value: msg.value, always: !!msg.always});

    } else if (msg.type === 'removeHeader') {
        var excl = msg.key.toLowerCase();
        _current = _current.filter(function(h) {
            return h.key.toLowerCase() !== excl;
        });

    } else if (msg.type === 'clearHeaders') {
        _current = [];
    }
}

onconnect = function(e) {
    var port = e.ports[0];
    _ports.push(port);

    // Send current state so the new worker catches up immediately.
    port.postMessage({type: 'setState', headers: _current.slice()});

    port.onmessage = function(ev) {
        var msg = ev.data;

        // Update canonical state.
        applyToState(msg);

        // Fan out to every OTHER port — the sender applied locally already.
        for (var i = 0; i < _ports.length; i++) {
            if (_ports[i] !== port) {
                _ports[i].postMessage(msg);
            }
        }
    };
};
