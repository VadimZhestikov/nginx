// D2.3 — WAF Lite: Request Body Scanner
//
// A hook on /api/submit/ reads the full request body with r.readBody()
// and scans it against a set of SQL injection and XSS patterns.
// Malicious requests are blocked with 400 Bad Request before they reach
// the content handler — no external WAF appliance required.
//
// Patterns checked (case-insensitive):
//   SQLi: SELECT, UNION, INSERT, UPDATE, DELETE, DROP, --, /*, xp_
//   XSS:  <script, javascript:, onerror=, onload=, alert(, document.cookie
//
// The pattern list is stored in a plain JS array — add rules at runtime by
// calling POST /waf/rules/ (see handler below).
//
// NOTE: r.readBody() buffers the full request body in the worker process.
// For large uploads use streaming body filters instead.  This demo targets
// the common API payload case (JSON / form data, typically < 64 KB).

(function () {
    var server = nginx.http.servers[0];

    // ----------------------------------------------------------------
    // WAF rule set — case-insensitive string patterns
    // ----------------------------------------------------------------
    var SQL_PATTERNS = [
        'select ', 'union ', 'insert ', 'update ', 'delete ', 'drop ',
        ' or ', ' and ', '--', '/*', 'xp_', 'exec(', 'execute('
    ];

    var XSS_PATTERNS = [
        '<script', 'javascript:', 'onerror=', 'onload=', 'onclick=',
        'alert(', 'document.cookie', 'eval(', '<iframe'
    ];

    var ALL_RULES = [];
    SQL_PATTERNS.forEach(function (p) { ALL_RULES.push({ pattern: p, type: 'sqli' }); });
    XSS_PATTERNS.forEach(function (p) { ALL_RULES.push({ pattern: p, type: 'xss' }); });

    function scanBody(body) {
        var lower = body.toLowerCase();
        for (var i = 0; i < ALL_RULES.length; i++) {
            var rule = ALL_RULES[i];
            if (lower.indexOf(rule.pattern) !== -1) {
                return { blocked: true, type: rule.type, pattern: rule.pattern };
            }
        }
        return { blocked: false };
    }

    // ----------------------------------------------------------------
    // /api/submit/ — scan body, reject or pass
    // ----------------------------------------------------------------
    var submitLoc = server.findLocation('/api/submit/');

    submitLoc.addHook(async function (r) {
        if (r.method !== 'POST') {
            return;  // only scan POST bodies
        }

        var body = await r.readBody();
        if (!body) {
            return;  // empty body — nothing to scan
        }

        var result = scanBody(body);
        if (result.blocked) {
            nginx.log(4, 'WAF blocked request: type=' + result.type +
                ' pattern="' + result.pattern + '"');
            r.respond(400, {
                'X-WAF-Blocked': result.type
            }, 'Blocked by WAF: ' + result.type + ' pattern detected\n');
            // hook returns without calling next — content handler skipped
        }
    });

    submitLoc.handler = function (r) {
        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            status: 'ok',
            message: 'Submission accepted'
        }) + '\n');
    };

    // ----------------------------------------------------------------
    // /waf/rules/ — inspect current WAF rule count
    // ----------------------------------------------------------------
    server.findLocation('/waf/rules/').handler = function (r) {
        var byType = {};
        ALL_RULES.forEach(function (rule) {
            byType[rule.type] = (byType[rule.type] || 0) + 1;
        });

        r.respond(200, {
            'Content-Type': 'application/json'
        }, JSON.stringify({
            total_rules: ALL_RULES.length,
            by_type: byType,
            patterns: ALL_RULES.map(function (r) { return r.pattern; })
        }, null, 2) + '\n');
    };

})();
