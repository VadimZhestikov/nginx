// D3.3 — Dev/Prod Parity via js_preprocess
//
// js_preprocess runs this script at nginx parse time — before any http{}
// block exists and before worker processes are forked.  The script reads
// the APP_ENV environment variable and emits an http{} block with the
// port and location set appropriate for the target environment.
//
// Environments:
//   dev  → port 8184, includes /debug/ location for internal state dump
//   prod → port 8185, /debug/ absent (not emitted at all)
//
// The SAME nginx.conf and gen.js serve both environments.
// APP_ENV is the only external input — config parity through a single
// code path.
//
// handler.js detects the environment by checking which locations were
// emitted (dev has /debug/, prod does not) — no env var needed at runtime.
//
// config.write(text) injects text back into nginx's parser as if it
// appeared inline in nginx.conf.

import * as std from 'std';

(function () {
    var rawEnv = std.getenv('APP_ENV') || 'dev';
    var env    = rawEnv.toLowerCase().trim();
    var isDev  = (env === 'dev' || env === 'development');

    var port = isDev ? 8184 : 8185;

    var debugBlock = isDev
        ? '        # Debug endpoint — dev only\n' +
          '        location /debug/ { }\n\n'
        : '';

    // Emit js_source first (top-level directive)
    config.write('js_source handler.js;\n');

    // Emit the http{} block
    config.write(
        'http {\n' +
        '    default_type  text/plain;\n' +
        '\n' +
        '    server {\n' +
        '        listen       ' + port + ';\n' +
        '        server_name  localhost;\n' +
        '\n' +
        '        location /api/ { }\n' +
        '        location /env/ { }\n' +
        '        location /error/ { }\n' +
        debugBlock +
        '    }\n' +
        '}\n'
    );

})();
