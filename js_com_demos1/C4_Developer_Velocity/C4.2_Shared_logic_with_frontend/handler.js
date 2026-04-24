// handler.js — nginx handler using the same normalizeUrl logic as the browser
//
// normalize.js is a pure ES module; nginx js_source doesn't support ES module
// imports directly, so we inline the function here.  In production, use
// js_preprocess to bundle or concatenate shared modules before evaluation.
//
// /normalize/?url=<raw> — returns the normalized URL as JSON

function normalizeUrl(url) {
    if (!url || typeof url !== 'string') return '/';
    var result = url.toLowerCase();
    result = result.replace(/\/+/g, '/');
    if (result.length > 1) result = result.replace(/\/$/, '');
    result = result.replace(/[^a-z0-9\/\-_\.]/g, '_');
    return result || '/';
}

(function () {
    var server = nginx.http.servers[0];
    var normLoc = server.findLocation('/normalize/');

    normLoc.handler = function (r) {
        var rawUrl = '';
        var match = r.args.match(/(?:^|&)url=([^&]*)/);
        if (match) rawUrl = decodeURIComponent(match[1]);

        // Also accept X-Raw-Url header (useful for testing)
        if (!rawUrl) rawUrl = r.headers['X-Raw-Url'] || r.headers['x-raw-url'] || '';

        var normalized = normalizeUrl(rawUrl);

        r.respond(200, { 'Content-Type': 'application/json' },
            JSON.stringify({ raw: rawUrl, normalized: normalized }) + '\n');
    };
}());
