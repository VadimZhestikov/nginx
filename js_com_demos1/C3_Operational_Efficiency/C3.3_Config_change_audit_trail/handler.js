// C3.3 — Config-change audit trail
//
// Every POST to /admin/change/ records the change in a cross-worker
// audit log stored in nginx.shared (circular buffer, last 20 entries).
// GET /api/audit/ returns the full log as JSON.
//
// Because nginx.shared is visible to all workers, the audit log is
// consistent regardless of which worker handles each request.
//
// POST /admin/change/  body: {"action":"<act>","user":"<user>","detail":"<detail>"}
// GET  /api/audit/            returns the audit log

var AUDIT_MAX = 20;
var SEQ_KEY   = 'audit:seq';
var LOG_KEY   = 'audit:log';

function getSeq() {
    var v = nginx.shared.get(SEQ_KEY);
    return v ? parseInt(v, 10) : 0;
}

function appendEntry(entry) {
    // Read current log
    var raw = nginx.shared.get(LOG_KEY) || '[]';
    var log;
    try { log = JSON.parse(raw); } catch (e) { log = []; }

    // Append and trim to last AUDIT_MAX
    log.push(entry);
    if (log.length > AUDIT_MAX) log = log.slice(log.length - AUDIT_MAX);

    nginx.shared.set(LOG_KEY, JSON.stringify(log));
    var seq = nginx.shared.incr(SEQ_KEY);
    return seq;
}

function getLog() {
    var raw = nginx.shared.get(LOG_KEY) || '[]';
    try { return JSON.parse(raw); } catch (e) { return []; }
}

(function () {
    var server = nginx.http.servers[0];

    // POST /admin/change/ — record a config change
    var changeLoc = server.findLocation('/admin/change/');
    changeLoc.handler = async function (r) {
        if (r.method !== 'POST') {
            r.respond(405, {}, 'POST required\n');
            return;
        }

        var body = await r.readBody();
        var change;
        try {
            change = JSON.parse(body);
        } catch (e) {
            r.respond(400, {}, 'invalid JSON body\n');
            return;
        }

        if (!change.action || !change.user) {
            r.respond(400, {}, 'body must have "action" and "user" fields\n');
            return;
        }

        var entry = {
            seq:       0,  // filled in below
            timestamp: new Date().toISOString(),
            action:    String(change.action),
            user:      String(change.user),
            detail:    String(change.detail || ''),
            sourceIp:  r.headers['X-Forwarded-For'] || '127.0.0.1'
        };

        var seq = appendEntry(entry);
        entry.seq = seq;

        // Also log to nginx error log
        nginx.log(5, 'AUDIT [' + seq + '] user=' + entry.user +
            ' action=' + entry.action + ' detail=' + entry.detail);

        r.respond(201, { 'Content-Type': 'application/json' },
            JSON.stringify({ ok: true, seq: seq, entry: entry }) + '\n');
    };

    // GET /api/audit/ — return the full audit log
    var auditLoc = server.findLocation('/api/audit/');
    auditLoc.handler = function (r) {
        var log = getLog();
        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ count: log.length, entries: log }, null, 2) + '\n');
    };
}());
