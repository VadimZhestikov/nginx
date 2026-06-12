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

    /* POST /admin/compact/:id */
    var compactMatch = uri.match(/^\/admin\/compact\/([^/]+)$/);
    if (m === 'POST' && compactMatch) {
        try {
            var removed = nginx.admin.compactSnapshot(compactMatch[1]);
            jsonOk(req, { id: compactMatch[1], removed: removed });
        } catch (e) {
            jsonErr(req, e.message && e.message.indexOf('not found') >= 0 ? 404 : 500,
                    e.message || String(e));
        }
        return true;
    }

    /* GET /admin/worker */
    if (m === 'GET' && uri === '/admin/worker') {
        jsonOk(req, { worker: req.variable('pid') });
        return true;
    }

    /* raw-snapshot, squash, set — handled by dispatchAsync (need readBody) */
    return false;
}

var adminLoc = nginx.http.servers[0].locations.find(function (l) {
    return l.path === '/admin/';
});

if (!adminLoc) {
    nginx.log('admin-api: /admin/ location not found; API disabled');
} else {
    adminLoc.handler = async function (req) {
        if (!dispatch(req)) {
            /* Routes that need an async body read */
            await dispatchAsync(req);
        }
    };
}

/*
 * dispatchAsync — handles routes that require await req.readBody().
 * Called only when the synchronous dispatch() returned false.
 */
async function dispatchAsync(req) {
    var uri = req.uri;
    var m   = req.method;

    /* POST /admin/raw-snapshot */
    if (m === 'POST' && uri === '/admin/raw-snapshot') {
        var rawText = await req.readBody();
        var rawBody = null;
        try { rawBody = JSON.parse(rawText || '{}'); } catch(e) {}
        if (!rawBody) { jsonErr(req, 400, 'invalid JSON body'); return; }
        var name = (rawBody && rawBody.name) || (req.queryParams && req.queryParams.name);
        if (!name) { jsonErr(req, 400, 'name required'); return; }
        if (!Array.isArray(rawBody.ops)) { jsonErr(req, 400, 'ops array required'); return; }
        try {
            jsonOk(req, { id: nginx.admin.createRawSnapshot(name, rawBody.ops) });
        } catch (e) { jsonErr(req, 500, e.message || String(e)); }
        return;
    }

    /* POST /admin/squash — merge snapshots; body: {"ids":[...],"name":"..."} */
    if (m === 'POST' && uri === '/admin/squash') {
        var sqText = await req.readBody();
        var sqBody = null;
        try { sqBody = JSON.parse(sqText || '{}'); } catch(e) {}
        if (!sqBody) { jsonErr(req, 400, 'invalid JSON body'); return; }
        if (!Array.isArray(sqBody.ids) || !sqBody.ids.length) {
            jsonErr(req, 400, 'ids array required'); return;
        }
        if (!sqBody.name) { jsonErr(req, 400, 'name required'); return; }
        try {
            jsonOk(req, { id: nginx.admin.squash(sqBody.ids, sqBody.name) });
        } catch (e) { jsonErr(req, 500, e.message || String(e)); }
        return;
    }

    /* POST /admin/set — set one nginx.shared key */
    if (m === 'POST' && uri === '/admin/set') {
        var setKey = req.queryParams && req.queryParams.key;
        if (setKey) {
            var setVal = (req.queryParams && req.queryParams.value) || '';
            nginx.shared.set(setKey, String(setVal));
            jsonOk(req, { key: setKey, value: setVal });
            return;
        }
        var setBody = null;
        try { setBody = JSON.parse((await req.readBody()) || '{}'); } catch(e) {}
        if (!setBody || !setBody.key) { jsonErr(req, 400, 'key required'); return; }
        nginx.shared.set(setBody.key, String(setBody.value !== undefined ? setBody.value : ''));
        jsonOk(req, { key: setBody.key, value: setBody.value });
        return;
    }

    jsonErr(req, 404, 'unknown admin route: ' + m + ' ' + uri);
}

})();
