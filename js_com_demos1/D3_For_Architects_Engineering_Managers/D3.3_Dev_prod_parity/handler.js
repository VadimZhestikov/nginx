// D3.3 — Handler for dev/prod environments
//
// handler.js is evaluated after gen.js has emitted the http{} block.
// The server port already tells us which environment we are in:
//   8184 → dev  (verbose responses)
//   8185 → prod (minimal responses)
//
// We detect the environment by checking which port nginx is listening on
// via nginx.http.servers[0].locations — or more simply, we sniff the
// port from the server address.  However, the simplest and most reliable
// approach is: check what locations gen.js emitted.  In dev, gen.js
// emits a /debug/ location; in prod it does not.  A findLocation() call
// distinguishes the two.

(function () {
    var server = nginx.http.servers[0];

    // Detect environment: dev has /debug/ location; prod does not
    var debugLoc = server.findLocation('/debug/');
    var isDev    = !!debugLoc;
    var envName  = isDev ? 'dev' : 'prod';

    // ----------------------------------------------------------------
    // /api/ — application endpoint
    // ----------------------------------------------------------------
    var apiLoc = server.findLocation('/api/');
    if (apiLoc) {
        apiLoc.handler = function (r) {
            if (isDev) {
                r.respond(200, {
                    'Content-Type': 'application/json',
                    'X-Env': envName,
                    'X-Debug': 'true'
                }, JSON.stringify({
                    message: 'Hello from ' + envName,
                    environment: envName,
                    debug_info: {
                        worker_idx: nginx.workerIdx,
                        nginx_version: nginx.version,
                        note: 'verbose dev mode — never expose this in prod'
                    }
                }, null, 2) + '\n');
            } else {
                r.respond(200, {
                    'Content-Type': 'application/json'
                }, JSON.stringify({ message: 'OK' }) + '\n');
            }
        };
    }

    // ----------------------------------------------------------------
    // /env/ — returns the active environment name
    // ----------------------------------------------------------------
    var envLoc = server.findLocation('/env/');
    if (envLoc) {
        envLoc.handler = function (r) {
            r.respond(200, {}, envName + '\n');
        };
    }

    // ----------------------------------------------------------------
    // /error/ — simulated error; dev = verbose, prod = generic
    // ----------------------------------------------------------------
    var errorLoc = server.findLocation('/error/');
    if (errorLoc) {
        errorLoc.handler = function (r) {
            if (isDev) {
                r.respond(500, {
                    'Content-Type': 'application/json',
                    'X-Env': envName
                }, JSON.stringify({
                    error: 'Simulated error',
                    environment: envName,
                    detail: 'This is a detailed error response for debugging.',
                    hint: 'Check logs/error.log for the full stack trace.'
                }, null, 2) + '\n');
            } else {
                r.respond(500, {}, 'Internal Server Error\n');
            }
        };
    }

    // ----------------------------------------------------------------
    // /debug/ — dev only (gen.js does not emit this location in prod)
    // ----------------------------------------------------------------
    if (debugLoc) {
        debugLoc.handler = function (r) {
            var servers = nginx.http.servers.map(function (s) {
                return {
                    name: s.name,
                    locations: s.locations.map(function (l) { return l.path; })
                };
            });
            r.respond(200, {
                'Content-Type': 'application/json',
                'X-Env': envName
            }, JSON.stringify({
                environment: envName,
                nginx_version: nginx.version,
                worker_processes: nginx.cpu_count,
                servers: servers
            }, null, 2) + '\n');
        };
    }

})();
