// B4.3 SharedWorker — DNS resolver daemon
//
// Periodically "resolves" hostnames and stores the current IP list.
// In a real deployment this worker would call os.exec(['getent', 'hosts', host])
// or open a UDP socket to a nameserver.  Here we simulate resolution with a
// round-robin rotation through a small set of demo IPs so that the /admin/refresh/
// endpoint visibly changes the result.
//
// Message protocol (per-connection via port):
//   {cmd: 'status'}   → {hosts: {hostname: [ip, ...], ...}, resolvedAt}
//   {cmd: 'refresh'}  → {ok: true, hosts: {...}, resolvedAt}

var HOSTNAMES = ['api.example.com', 'db.example.com'];

// Simulated IP pools — each refresh cycles to the next entry
var IP_POOLS = {
    'api.example.com': [
        ['93.184.216.34', '93.184.216.35'],
        ['93.184.216.36', '93.184.216.34']
    ],
    'db.example.com': [
        ['10.0.1.10'],
        ['10.0.1.11']
    ]
};

var cycleIdx   = 0;
var resolvedAt = new Date().toISOString();

function currentHosts() {
    var result = {};
    HOSTNAMES.forEach(function (h) {
        result[h] = IP_POOLS[h][cycleIdx % IP_POOLS[h].length];
    });
    return result;
}

var state = {hosts: currentHosts(), resolvedAt: resolvedAt};

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (msg) {
        var data = msg.data;

        if (data.cmd === 'status') {
            port.postMessage({hosts: state.hosts, resolvedAt: state.resolvedAt});

        } else if (data.cmd === 'refresh') {
            cycleIdx++;
            state.hosts      = currentHosts();
            state.resolvedAt = new Date().toISOString();
            port.postMessage({ok: true, hosts: state.hosts, resolvedAt: state.resolvedAt});

        } else {
            port.postMessage({error: 'unknown command: ' + data.cmd});
        }
    };
};
