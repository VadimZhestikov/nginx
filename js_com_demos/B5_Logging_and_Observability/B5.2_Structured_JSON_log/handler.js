// B5.2 — Structured JSON Access Log
//
// Logs each request as a single-line JSON object to the nginx error log.
// The log entry includes:
//   ts        ISO-8601 timestamp
//   method    HTTP verb
//   uri       request URI
//   ua        User-Agent (truncated to 80 chars)
//   ref       Referer (if present)
//   wid       nginx.workerIdx — which worker handled the request
//
// Classic nginx: the access_log directive supports custom log_format strings,
// but they are static templates.  Emitting valid JSON requires carefully
// escaping every variable (quotes, backslashes, control characters) — a
// well-known pain point.  Variable values that contain quotes or newlines will
// break JSON parsers.  In JavaScript, JSON.stringify handles all escaping
// automatically and the structure is defined as a plain object literal.

(function () {
    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/api/'; });
    });

    var loc = server.locations.find(function (l) { return l.path === '/api/'; });

    // Request hook — fires before the handler, records structured log entry
    loc.addHook(function (r, next) {
        var entry = {
            ts:     new Date().toISOString(),
            method: r.method,
            uri:    r.uri,
            ua:     (r.headers['user-agent']  || '').slice(0, 80),
            ref:    r.headers['referer']      || '',
            wid:    nginx.workerIdx
        };

        // nginx.log level 5 = INFO — appears in error.log at "info" severity
        nginx.log(5, 'access ' + JSON.stringify(entry));

        next(r);
    });

    // Handler
    loc.handler = function (r) {
        r.respond(200, {}, 'OK\n');
    };
})();
