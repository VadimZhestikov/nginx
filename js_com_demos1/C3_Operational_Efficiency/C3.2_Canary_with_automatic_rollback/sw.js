// C3.2 SharedWorker — canary error-rate monitor
//
// Tracks 5xx responses in a sliding window.  If the error rate
// exceeds the threshold, sets mode to 'rollback'.
//
// Uses onconnect + port.onmessage protocol.
//
// Protocol:
//   {cmd:'record', status:<number>}  — report a response status
//   {cmd:'status'}                   — get current mode + stats
//   {cmd:'enable'}                   — enable canary mode
//   {cmd:'disable'}                  — force rollback (manual)
//   {cmd:'reset'}                    — clear error counts

var WINDOW_SIZE     = 20;   // track last N requests
var ERROR_THRESHOLD = 0.4;  // rollback if > 40% are 5xx

var history = [];  // ring buffer of booleans (true = 5xx)
var mode    = 'stable';   // 'canary' | 'rollback' | 'stable'

function computeErrorRate() {
    if (history.length === 0) return 0;
    var errors = history.filter(function (e) { return e; }).length;
    return errors / history.length;
}

function recordStatus(status) {
    var is5xx = status >= 500;
    history.push(is5xx);
    if (history.length > WINDOW_SIZE) history.shift();

    if (mode === 'canary' && computeErrorRate() > ERROR_THRESHOLD) {
        mode = 'rollback';
    }
}

onconnect = function (e) {
    var port = e.ports[0];

    port.onmessage = function (msg) {
        var d = msg.data;

        if (d.cmd === 'record') {
            recordStatus(d.status);
            port.postMessage({ ok: true, mode: mode, errorRate: computeErrorRate() });

        } else if (d.cmd === 'status') {
            port.postMessage({
                mode:       mode,
                errorRate:  computeErrorRate(),
                samples:    history.length,
                threshold:  ERROR_THRESHOLD
            });

        } else if (d.cmd === 'enable') {
            mode    = 'canary';
            history = [];
            port.postMessage({ ok: true, mode: mode });

        } else if (d.cmd === 'disable') {
            mode = 'rollback';
            port.postMessage({ ok: true, mode: mode });

        } else if (d.cmd === 'reset') {
            mode    = 'stable';
            history = [];
            port.postMessage({ ok: true, mode: mode });

        } else {
            port.postMessage({ error: 'unknown command: ' + d.cmd });
        }
    };
};
