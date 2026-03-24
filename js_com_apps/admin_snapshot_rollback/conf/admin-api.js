import * as std from 'std';

/*
 * admin-api.js — HTTP REST handlers for the admin snapshot/rollback API.
 *
 * Requires admin.js to be loaded first (nginx.admin must exist).
 *
 * Routes (all under the /admin/ location):
 *
 *   GET  /admin/snapshots         — list snapshot ids as JSON array
 *   POST /admin/snapshots         — create snapshot; body JSON {"name":"..."}
 *   GET  /admin/snapshots/:id     — return snapshot JSON content
 *   POST /admin/apply/:id         — apply named snapshot
 *   POST /admin/rollback          — rollback to previous snapshot
 *   GET  /admin/state             — return current delta state as JSON
 *
 * Response format:
 *   Success: 200, Content-Type: application/json, body: JSON
 *   Error:   4xx/5xx, Content-Type: application/json,
 *            body: {"error":"<message>"}
 */

(function () {

function jsonOk(req, data) {
    req.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(data) + '\n');
}

function jsonErr(req, status, msg) {
    req.respond(status, {'Content-Type': 'application/json'},
                JSON.stringify({error: String(msg)}) + '\n');
}

function readBody(req) {
    /* NginxRequest.body is a string (already buffered for small bodies) */
    var body = req.body;
    if (!body) { return {}; }
    try { return JSON.parse(body); } catch (e) { return null; }
}

/*
 * Route dispatch: match method + path pattern.
 * Returns true if the request was handled, false otherwise.
 */
function dispatch(req) {
    var uri = req.uri;   /* e.g. "/admin/snapshots" */
    var m   = req.method;

    /* GET /admin/state */
    if (m === 'GET' && uri === '/admin/state') {
        jsonOk(req, nginx.admin.state());
        return true;
    }

    /* GET /admin/snapshots */
    if (m === 'GET' && uri === '/admin/snapshots') {
        jsonOk(req, nginx.admin.listSnapshots());
        return true;
    }

    /* POST /admin/snapshots — create snapshot */
    if (m === 'POST' && uri === '/admin/snapshots') {
        var body = readBody(req);
        if (!body) { jsonErr(req, 400, 'invalid JSON body'); return true; }
        /* name can come from JSON body or query param */
        var name = (body && body.name) || req.queryParams.name;
        if (!name) { jsonErr(req, 400, 'name required'); return true; }
        try {
            var id = nginx.admin.createSnapshot(name);
            jsonOk(req, {id: id});
        } catch (e) {
            jsonErr(req, 500, e.message || String(e));
        }
        return true;
    }

    /* GET /admin/snapshots/:id */
    var snapMatch = uri.match(/^\/admin\/snapshots\/([^/]+)$/);
    if (m === 'GET' && snapMatch) {
        var snapId = snapMatch[1];
        var path   = nginx.cycle.prefix + 'snapshots/' + snapId + '.json';
        var text   = std.loadFile(path);
        if (!text) { jsonErr(req, 404, 'snapshot not found: ' + snapId); return true; }
        req.respond(200, {'Content-Type': 'application/json'}, text);
        return true;
    }

    /* POST /admin/apply/:id */
    var applyMatch = uri.match(/^\/admin\/apply\/([^/]+)$/);
    if (m === 'POST' && applyMatch) {
        var applyId = applyMatch[1];
        try {
            nginx.admin.applySnapshot(applyId);
            jsonOk(req, {applied: applyId});
        } catch (e) {
            jsonErr(req, e.message && e.message.indexOf('not found') >= 0 ? 404 : 500,
                    e.message || String(e));
        }
        return true;
    }

    /* POST /admin/rollback */
    if (m === 'POST' && uri === '/admin/rollback') {
        try {
            var prev = nginx.admin.rollback();
            jsonOk(req, {rolledBackTo: prev || 'base'});
        } catch (e) {
            jsonErr(req, 500, e.message || String(e));
        }
        return true;
    }

    return false;
}

var adminLoc = nginx.http.servers[0].locations.find(function (l) {
    return l.path === '/admin/';
});

if (!adminLoc) {
    nginx.log('admin-api: /admin/ location not found; API disabled');
} else {
    adminLoc.handler = function (req) {
        if (!dispatch(req)) {
            jsonErr(req, 404, 'unknown admin route: ' + req.method + ' ' + req.uri);
        }
    };
}

})();
