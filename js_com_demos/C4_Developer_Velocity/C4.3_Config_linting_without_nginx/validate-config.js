// validate-config.js — validate a plugin config JSON against a schema
// Run with: qjs validate-config.js [config-file]
//
// Exit code 0 = valid, 1 = errors found or file unreadable.

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
    // Optional: enumerated allowed values
    enums: {
        logLevel: ['debug', 'info', 'warn', 'error']
    }
};

function validate(cfg, s) {
    var errors = [];

    // Required fields
    (s.required || []).forEach(function (k) {
        if (cfg[k] === undefined || cfg[k] === null)
            errors.push('missing required field: "' + k + '"');
    });

    // Type checks
    Object.keys(s.types || {}).forEach(function (k) {
        if (cfg[k] !== undefined && cfg[k] !== null) {
            var expected = s.types[k];
            var actual   = typeof cfg[k];
            if (actual !== expected)
                errors.push('"' + k + '": expected ' + expected + ', got ' + actual +
                    ' (value: ' + JSON.stringify(cfg[k]) + ')');
        }
    });

    // Range checks
    Object.keys(s.ranges || {}).forEach(function (k) {
        var v = cfg[k];
        var r = s.ranges[k];
        if (v !== undefined && v !== null && typeof v === 'number') {
            if (v < r[0] || v > r[1])
                errors.push('"' + k + '": value ' + v +
                    ' is out of range [' + r[0] + ', ' + r[1] + ']');
        }
    });

    // Enum checks
    Object.keys(s.enums || {}).forEach(function (k) {
        var v = cfg[k];
        var allowed = s.enums[k];
        if (v !== undefined && v !== null && allowed.indexOf(v) === -1)
            errors.push('"' + k + '": "' + v + '" is not one of [' +
                allowed.map(function (a) { return '"' + a + '"'; }).join(', ') + ']');
    });

    return errors;
}

// ── Main ──────────────────────────────────────────────────────────────────

var configFile = (typeof scriptArgs !== 'undefined' && scriptArgs[1]) || 'good-config.json';

var cfg;
try {
    var text = std.loadFile(configFile);
    if (text === null) throw new Error('file not found: ' + configFile);
    cfg = JSON.parse(text);
} catch (e) {
    print('ERROR: Cannot load config "' + configFile + '": ' + e.message);
    std.exit(1);
}

print('Validating: ' + configFile);
print('Config: ' + JSON.stringify(cfg, null, 2));
print('');

var errors = validate(cfg, schema);

if (errors.length === 0) {
    print('OK: config is valid');
    std.exit(0);
} else {
    print('ERRORS (' + errors.length + '):');
    errors.forEach(function (e) { print('  - ' + e); });
    std.exit(1);
}
