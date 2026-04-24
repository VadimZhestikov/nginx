// B4.1 SharedWorker — backend health monitor
//
// Maintains health state for a set of simulated backend peers.
// In a real deployment this worker would open HTTP connections to each peer's
// /_health endpoint every few seconds.  Here the state is updated via admin
// messages to keep the demo self-contained and deterministic.
//
// Message protocol (per-connection via port):
//   {cmd: 'status'}             → {peers: [{addr, healthy}, ...]}
//   {cmd: 'set', addr, healthy} → {ok: true}

var peers = [
    {addr: '127.0.0.1:8142', healthy: true},
    {addr: '127.0.0.1:8143', healthy: true}
];

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (msg) {
        var data = msg.data;

        if (data.cmd === 'status') {
            port.postMessage({peers: peers.map(function (p) {
                return {addr: p.addr, healthy: p.healthy};
            })});

        } else if (data.cmd === 'set') {
            var peer = peers.find(function (p) { return p.addr === data.addr; });
            if (peer) {
                peer.healthy = data.healthy;
            }
            port.postMessage({ok: true});

        } else {
            port.postMessage({error: 'unknown command: ' + data.cmd});
        }
    };
};
