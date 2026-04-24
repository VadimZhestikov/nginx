// B3.3 — Response Fan-out / Fan-in
//
// Makes sequential subrequests to three internal micro-endpoints and merges
// their JSON responses into a single aggregated product object.
//
// In production a GraphQL gateway or BFF (Backend-for-Frontend) service does
// this job — a separate process with its own network stack.  Here nginx acts
// as the aggregator: all three subrequests are handled by the same worker
// event loop, the results are merged in JavaScript, and the client receives a
// single JSON response.
//
// Note: subrequests must be sequential (await one, then the next).
// Promise.all([r.subrequest(A), r.subrequest(B), r.subrequest(C)]) crashes the
// worker with SIGABRT / heap corruption.  Root cause: each subrequest's
// completion calls ngx_http_finalize_request → ngx_http_run_posted_requests,
// which recursively processes the next posted event.  With three subrequests
// in-flight simultaneously, the recursive chain eventually runs the parent's
// write_event_handler (ngx_js_subreq_resume) while earlier subrequests' C
// frames are still live on the stack.  ngx_js_subreq_resume → r.respond() →
// ngx_http_finalize_connection frees the parent request pool, leaving those
// stacked frames with dangling pointers — hence the heap corruption.
// Sequential awaits are safe because the subrequest call stack fully unwinds
// before the next event-loop tick runs the parent's resume handler.

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

            // Sequential subrequests — fan-out to three internal services
            var priceRes  = await r.subrequest('/internal/price/?id='  + id);
            var stockRes  = await r.subrequest('/internal/stock/?id='  + id);
            var ratingRes = await r.subrequest('/internal/rating/?id=' + id);

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
