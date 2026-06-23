// safe-config cfgworker — config-authority SharedWorker for the safe-config
// plugin's cross-worker fan-out.
//
// Uses the nginx SharedWorker onconnect/port pattern:
//   - each nginx worker connects on activation; its port is added to _ports[]
//   - an {type:'op', cmd} message from one worker is relayed to all OTHER
//     workers (the sender has already applied it locally — skip-sender guard)
//   - {type:'hello'} just establishes the connection; no relay
//
// The cmd payload is opaque to the SW; each receiving worker interprets it
// via the plugin's local execCmd().  cmd is always JSON-serialisable
// ({kind:'set', path, value} or {kind:'toggle', serverName, locPath, on}).

var _ports = [];

onconnect = function (e) {
    var port = e.ports[0];
    _ports.push(port);

    port.onmessage = function (ev) {
        if (!ev.data) { return; }

        if (ev.data.type === 'op') {
            /* Relay to every OTHER worker; the sender applied it locally. */
            _ports.forEach(function (p) {
                if (p !== port) { p.postMessage(ev.data); }
            });
        }
        /* 'hello' carries no action — it just registers the port. */
    };
};
