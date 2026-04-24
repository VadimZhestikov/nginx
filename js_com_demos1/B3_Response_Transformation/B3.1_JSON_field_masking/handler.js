// B3.1 — JSON Field Masking
//
// A body filter intercepts the JSON response and:
//   - Masks the "ssn" field (replaces value with "***")
//   - Removes the "internal" field entirely
//   - Leaves all other fields untouched
//
// Classic nginx: ngx_http_sub_module can replace literal strings, but it
// cannot parse JSON or perform field-aware transformations.  Masking a field
// value while preserving surrounding JSON structure is impossible without
// Lua or an upstream application.  The JS body filter receives the complete
// response body as a string and returns a modified string — straightforward
// JSON manipulation.

(function () {
    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/data/'; });
    });

    var loc = server.locations.find(function (l) { return l.path === '/data/'; });

    // Handler — produces the "raw" response with sensitive data
    loc.handler = function (r) {
        var raw = {
            name:     'Alice',
            email:    'alice@example.com',
            ssn:      '123-45-6789',
            dob:      '1985-03-15',
            internal: 'cost-center-42-budget-overflow',
            role:     'engineer'
        };
        r.respond(200,
            {'Content-Type': 'application/json'},
            JSON.stringify(raw) + '\n');
    };

    // Body filter — masks sensitive fields before the response leaves nginx
    loc.addBodyFilter('wholeBodySync', function (r, body) {
        var obj;
        try {
            obj = JSON.parse(body);
        } catch (e) {
            // Not JSON — pass through unchanged
            return body;
        }

        if (obj.ssn !== undefined) {
            obj.ssn = '***';
        }

        delete obj.internal;

        return JSON.stringify(obj) + '\n';
    });
})();
