// B2.2 SharedWorker — API key blocklist
//
// Maintains a Set of revoked API keys.  Workers communicate via the SharedWorker
// MessagePort protocol (onconnect + port.onmessage):
//   {cmd: 'check', key: '...'}  → replies {allowed: true/false}
//   {cmd: 'revoke', key: '...'}  → replies {revoked: true, key: '...'}
//   {cmd: 'list'}               → replies {blocklist: [...]}
//
// In a production system the refresh interval would reload the list from an
// external source (database, config file, external API).

var blocklist = new Set();

// Pre-revoke a demo key so the test can show an already-blocked key
blocklist.add('revoked-at-startup');

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (msg) {
        var data = msg.data;
        var cmd  = data.cmd;

        if (cmd === 'check') {
            port.postMessage({allowed: !blocklist.has(data.key)});

        } else if (cmd === 'revoke') {
            blocklist.add(data.key);
            port.postMessage({revoked: true, key: data.key});

        } else if (cmd === 'list') {
            var keys = [];
            blocklist.forEach(function (k) { keys.push(k); });
            port.postMessage({blocklist: keys});

        } else {
            port.postMessage({error: 'unknown command: ' + cmd});
        }
    };
};
