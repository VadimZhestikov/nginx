// B3.2 — Conditional HTML Injection
//
// Injects a <script> tag into the HTML response only when the visitor has an
// active "session" cookie — a common pattern for injecting analytics, feature
// flags, or debugging tools for authenticated users without modifying the
// upstream application.
//
// Classic nginx: ngx_http_addition_module can prepend/append static strings,
// and ngx_http_sub_module can replace fixed patterns — but neither can
// conditionally inject based on a request header or cookie value.  The only
// stock solution requires recompiling with Lua (OpenResty).  The JS body filter
// reads the cookie from the request object, decides whether to inject, and
// splices the tag in at exactly the right position.

(function () {
    var INJECT_SCRIPT =
        '<script src="/static/session-tools.js" ' +
        'data-session="1" defer></script>';

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/page/'; });
    });

    var loc = server.locations.find(function (l) { return l.path === '/page/'; });

    // Handler — returns a minimal but realistic HTML page
    loc.handler = function (r) {
        var html = [
            '<!DOCTYPE html>',
            '<html lang="en">',
            '<head>',
            '  <meta charset="UTF-8">',
            '  <title>Demo Page</title>',
            '</head>',
            '<body>',
            '  <h1>Welcome</h1>',
            '  <p>This is the demo page.</p>',
            '</body>',
            '</html>'
        ].join('\n') + '\n';

        r.respond(200, {'Content-Type': 'text/html'}, html);
    };

    // Body filter — conditionally inject <script> before </head>
    loc.addBodyFilter('wholeBodySync', function (r, body) {
        // Parse cookies from the original request
        var cookieHeader = r.headers['cookie'] || '';
        var hasSession   = false;

        cookieHeader.split(';').forEach(function (part) {
            var kv  = part.trim().split('=');
            var key = kv[0].trim();
            if (key === 'session' && kv.length >= 2 && kv[1].trim()) {
                hasSession = true;
            }
        });

        if (!hasSession) {
            return body;
        }

        // Inject just before the closing </head> tag
        var insertAt = body.indexOf('</head>');
        if (insertAt === -1) {
            return body;
        }

        return body.slice(0, insertAt) +
               '  ' + INJECT_SCRIPT + '\n' +
               body.slice(insertAt);
    });
})();
