// A3.2 — Environment-Driven Config Generation
//
// js_preprocess script: reads APP_ENV at parse time and generates either
// a dev server (port 8118) or a prod server (port 8119).
//
// The same nginx.conf works in all environments — the environment variable
// drives the actual configuration that nginx uses.

import * as std from 'std';

var env = std.getenv('APP_ENV') || 'dev';

var conf;

if (env === 'prod') {
    conf = 'http {\n' +
    '    default_type text/plain;\n\n' +
    '    server {\n' +
    '        listen       8119;\n' +
    '        server_name  localhost;\n\n' +
    '        location / {\n' +
    '            return 200 "env=prod port=8119\\n";\n' +
    '        }\n\n' +
    '        location /health/ {\n' +
    '            return 200 "prod-ok\\n";\n' +
    '        }\n' +
    '    }\n' +
    '}\n';
} else {
    // dev (default) — more permissive, debug info in responses
    conf = 'http {\n' +
    '    default_type text/plain;\n\n' +
    '    server {\n' +
    '        listen       8118;\n' +
    '        server_name  localhost;\n\n' +
    '        location / {\n' +
    '            return 200 "env=dev port=8118 [debug mode]\\n";\n' +
    '        }\n\n' +
    '        location /health/ {\n' +
    '            return 200 "dev-ok\\n";\n' +
    '        }\n' +
    '    }\n' +
    '}\n';
}

config.write(conf);
