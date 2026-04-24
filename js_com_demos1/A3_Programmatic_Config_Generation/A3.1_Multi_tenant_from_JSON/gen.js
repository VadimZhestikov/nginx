// A3.1 — Multi-Tenant Server Generation from JSON
//
// js_preprocess script: runs during nginx config parsing, before init_conf.
// Reads tenants.json from the same directory as this script and generates
// one server{} block per tenant.
//
// Available globals:
//   config.write(text)  — injects text into the nginx config parser
//   std.loadFile(path)  — reads a file (QuickJS std module)
//   os.getcwd()         — current working directory (demo dir when run with -p .)

import * as std from 'std';
import * as os  from 'os';

// When nginx is run with -p <demo-dir>, the cwd IS the demo dir.
// std.loadFile resolves relative to cwd.
var raw = std.loadFile('tenants.json');
if (!raw) {
    // Fallback: try next to the script file using os.getcwd()
    var cwd = os.getcwd()[0] || '.';
    raw = std.loadFile(cwd + '/tenants.json');
}
if (!raw) {
    throw new Error('Could not read tenants.json');
}

var tenants = JSON.parse(raw);

// Emit the http{} wrapper and one server per tenant
var conf = 'http {\n    default_type text/plain;\n\n';

tenants.forEach(function (t) {
    conf += '    server {\n';
    conf += '        listen       ' + t.port + ';\n';
    conf += '        server_name  ' + t.name + '.local;\n';
    conf += '\n';
    conf += '        location / {\n';
    conf += '            return 200 "' + t.greeting + '\\n";\n';
    conf += '        }\n';
    conf += '\n';
    conf += '        location /health/ {\n';
    conf += '            return 200 "' + t.name + '-ok\\n";\n';
    conf += '        }\n';
    conf += '    }\n\n';
});

conf += '}\n';

config.write(conf);
