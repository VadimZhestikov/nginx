nginx.broadcast(function () {
    var locs = nginx.http.servers[0].locations;

    function loc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    const hdrs = {}

    loc('/empty').handler = function (r) {
        r.respond(200, hdrs, 'ok\n');
    };

    loc('/headers').handler = function (r) {
        var h   = r.headers;
        var out = '';
        for (var k in h) {
            out += k + ': ' + h[k] + '\n';
        }
        r.respond(200, hdrs, out);
    };
});
