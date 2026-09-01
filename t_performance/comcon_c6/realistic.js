onRequest(function(req) {
    var parts = req.uri.split("/");
    var out = { method: req.method, uri: req.uri, n: parts.length,
                tag: "t" + (parts.length * 7 + 3) };
    return JSON.stringify(out) + "\n";
});
