// handler.js — same validation logic exposed as an HTTP endpoint
//
// POST /validate/ with a JSON body → runs the same schema validation as
// validate-config.js and returns {ok, errors} as JSON.
//
// Demonstrates that the validation schema can be used at two levels:
//   1. Offline CI gate: qjs validate-config.js config.json
//   2. Runtime API gate: POST /validate/ from deployment tooling

var schema = {
    required: ['port', 'maxConnections'],
    types: {
        port:           'number',
        maxConnections: 'number',
        debug:          'boolean',
        name:           'string',
        timeout:        'number'
    },
    ranges: {
        port:           [1, 65535],
        maxConnections: [1, 10000],
        timeout:        [100, 300000]
    },
    enums: {
        logLevel: ['debug', 'info', 'warn', 'error']
    }
};

function validate(cfg, s) {
    var errors = [];
    (s.required || []).forEach(function (k) {
        if (cfg[k] === undefined || cfg[k] === null)
            errors.push('missing required field: "' + k + '"');
    });
    Object.keys(s.types || {}).forEach(function (k) {
        if (cfg[k] !== undefined && cfg[k] !== null) {
            var expected = s.types[k];
            var actual   = typeof cfg[k];
            if (actual !== expected)
                errors.push('"' + k + '": expected ' + expected + ', got ' + actual);
        }
    });
    Object.keys(s.ranges || {}).forEach(function (k) {
        var v = cfg[k]; var r = s.ranges[k];
        if (v !== undefined && v !== null && typeof v === 'number') {
            if (v < r[0] || v > r[1])
                errors.push('"' + k + '": ' + v + ' out of range [' + r[0] + ',' + r[1] + ']');
        }
    });
    Object.keys(s.enums || {}).forEach(function (k) {
        var v = cfg[k]; var allowed = s.enums[k];
        if (v !== undefined && v !== null && allowed.indexOf(v) === -1)
            errors.push('"' + k + '": "' + v + '" not in [' + allowed.join(', ') + ']');
    });
    return errors;
}

(function () {
    var server = nginx.http.servers[0];
    var validateLoc = server.findLocation('/validate/');

    validateLoc.handler = async function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'POST required\n');
            return;
        }

        var body = await r.readBody();
        var cfg;
        try {
            cfg = JSON.parse(body);
        } catch (e) {
            r.respond(400, { 'Content-Type': 'application/json' },
                JSON.stringify({ ok: false, errors: ['invalid JSON: ' + e.message] }) + '\n');
            return;
        }

        var errors = validate(cfg, schema);
        var ok     = errors.length === 0;
        r.respond(ok ? 200 : 422,
            { 'Content-Type': 'application/json' },
            JSON.stringify({ ok: ok, errors: errors }, null, 2) + '\n');
    };
}());
