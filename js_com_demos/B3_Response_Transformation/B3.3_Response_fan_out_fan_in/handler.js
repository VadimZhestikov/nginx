// B3.3 — Response Fan-out / Fan-in
//
// Issues three parallel subrequests to internal micro-endpoints with
// Promise.all and merges their JSON responses into a single aggregated
// product object.
//
// In production a GraphQL gateway or BFF (Backend-for-Frontend) service does
// this job — a separate process with its own network stack.  Here nginx acts
// as the aggregator: all three subrequests run in the same worker event loop,
// the results are merged in JavaScript, and the client receives a single JSON
// response.

(function () {
    function parseArgs(qs) {
        var out = {};
        (qs || '').split('&').forEach(function (part) {
            var kv = part.split('=');
            if (kv[0]) out[decodeURIComponent(kv[0])] = decodeURIComponent(kv[1] || '');
        });
        return out;
    }

    var servers = nginx.http.servers;
    var server  = servers.find(function (s) {
        return s.locations.some(function (l) { return l.path === '/product/'; });
    });
    var locs = server.locations;

    // Internal micro-endpoint: pricing service
    locs.find(function (l) { return l.path === '/internal/price/'; })
        .handler = function (r) {
            var id  = parseArgs(r.args)['id'] || '1';
            var obj = {price: 29.99 + parseInt(id, 10) * 0.50, currency: 'USD'};
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(obj) + '\n');
        };

    // Internal micro-endpoint: inventory service
    locs.find(function (l) { return l.path === '/internal/stock/'; })
        .handler = function (r) {
            var id  = parseArgs(r.args)['id'] || '1';
            var obj = {inStock: true, quantity: 100 - parseInt(id, 10) * 3};
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(obj) + '\n');
        };

    // Internal micro-endpoint: reviews service
    locs.find(function (l) { return l.path === '/internal/rating/'; })
        .handler = function (r) {
            var id  = parseArgs(r.args)['id'] || '1';
            var obj = {rating: 4.5, reviews: 128 + parseInt(id, 10) * 7};
            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(obj) + '\n');
        };

    // Aggregated product endpoint
    locs.find(function (l) { return l.path === '/product/'; })
        .handler = async function (r) {
            var id = parseArgs(r.args)['id'] || '1';

            // Parallel fan-out to three internal services
            var results = await Promise.all([
                r.subrequest('/internal/price/?id='  + id),
                r.subrequest('/internal/stock/?id='  + id),
                r.subrequest('/internal/rating/?id=' + id)
            ]);
            var priceRes  = results[0];
            var stockRes  = results[1];
            var ratingRes = results[2];

            // Parse each partial response
            function parse(res) {
                try { return JSON.parse(res.body); } catch (e) { return {}; }
            }

            // Fan-in: merge into a single product object
            var product = {
                id:     parseInt(id, 10),
                price:  parse(priceRes),
                stock:  parse(stockRes),
                rating: parse(ratingRes)
            };

            r.respond(200, {'Content-Type': 'application/json'},
                JSON.stringify(product, null, 2) + '\n');
        };
})();
