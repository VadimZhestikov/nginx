// D3.2 — Single Pane Admin UI (placeholder)
//
// This is a minimal placeholder demonstrating the JSON bridge pattern
// that the full admin_ui_demo_v2 uses to expose nginx state to a browser.
//
// Full admin UI: see admin_ui_demo_v2 in the repo root.

(function () {
    var server = nginx.http.servers[0];

    // /status/ — machine-readable nginx state summary
    server.findLocation('/status/').handler = function (r) {
        var servers = nginx.http.servers.map(function (s) {
            return {
                name:      s.name,
                locations: s.locations.map(function (l) { return l.path; })
            };
        });

        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            nginx_version: nginx.version,
            worker_count:  nginx.cpu_count,
            server_count:  servers.length,
            servers:       servers
        }, null, 2) + '\n');
    };

    // /info/ — human-readable note pointing to the full admin UI
    server.findLocation('/info/').handler = function (r) {
        r.respond(200, {}, [
            'D3.2 Admin UI placeholder',
            '',
            'This demo confirms the JSON bridge pattern.',
            'The full browser-based admin UI is in admin_ui_demo_v2/.',
            '',
            'Run: cd admin_ui_demo_v2 && bash test.sh',
            'Then open: http://127.0.0.1:8099/',
            ''
        ].join('\n'));
    };

})();
