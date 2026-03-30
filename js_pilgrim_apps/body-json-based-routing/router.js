/**
 * Body JSON Router — JS-Pilgrim reference application.
 *
 * Routes incoming POST requests to different nginx upstream pools based on
 * fields extracted from the JSON request body.  Only the bytes needed to
 * satisfy the routing decision are parsed; the rest of the body is
 * fast-forwarded or skipped entirely once all required fields are found.
 *
 * Default routing table (edit CFG.routes to customise):
 *
 *   service=payments  + tenant.region=eu-west  →  /internal/payments-eu/
 *   service=payments  + tenant.region=us-east  →  /internal/payments-us/
 *   service=analytics (any region)             →  /internal/analytics/
 *   anything else                              →  /internal/default/
 *
 * Body forwarding:
 *   The full request body is forwarded to the chosen backend via the
 *   X-Forwarded-Body request header.  This is reliable for bodies up to
 *   ~8 KB (nginx default large_client_header_buffers limit).  For larger
 *   bodies the right extension is req.setVariable() so proxy_pass can
 *   stream the body directly — see the comment in the content handler.
 *
 * Usage (nginx.conf):
 *   js_source /path/to/router.js;
 */

(function () {
    'use strict';

    /* ------------------------------------------------------------------
     * Configuration
     * ------------------------------------------------------------------ */

    var CFG = {
        /* JSON dot-paths whose values drive the routing decision.      */
        routingPaths: ['service', 'tenant.region'],

        /* Routing table: evaluated in order, first match wins.
         * A key absent from `match` is treated as a wildcard — only
         * the listed fields must match.                                 */
        routes: [
            { match: { 'service': 'payments', 'tenant.region': 'eu-west' },
              backend: '/internal/payments-eu/' },
            { match: { 'service': 'payments', 'tenant.region': 'us-east' },
              backend: '/internal/payments-us/' },
            { match: { 'service': 'analytics' },
              backend: '/internal/analytics/' },
        ],
        defaultBackend: '/internal/default/',

        /* Bodies larger than this threshold emit a WARN log entry.
         * Routing still works, but X-Forwarded-Body may be truncated
         * by upstream header-size limits.  Default nginx limit: 8 KB.  */
        bodyWarnSize: 8192,
    };

    /* ------------------------------------------------------------------
     * collectJsonPaths — partial JSON scanner
     *
     * Walks a JSON string and captures the values at the given dot-paths.
     * Stops scanning as soon as every required path has been found.
     * Subtrees that cannot lead to any required path are fast-forwarded
     * with skipValue() rather than recursed into.
     *
     * @param  {string}   json   Raw JSON string
     * @param  {string[]} paths  Dot-notation paths, e.g. ['a.b.c', 'type']
     * @returns {Object}  Map: path → JS value (path omitted if not found)
     * ------------------------------------------------------------------ */

    function collectJsonPaths(json, paths) {

        /* Build O(1) lookup tables from the requested path list. */
        var needed   = Object.create(null);  /* exact path  → true */
        var ancestor = Object.create(null);  /* prefix path → true */

        for (var i = 0; i < paths.length; i++) {
            needed[paths[i]] = true;
            var segs = paths[i].split('.');
            for (var d = 1; d < segs.length; d++) {
                ancestor[segs.slice(0, d).join('.')] = true;
            }
        }

        var result = Object.create(null);
        var remain = paths.length;   /* paths still to find */
        var pos    = 0;
        var stack  = [];             /* current key-path segments */
        var n      = json.length;

        /* ---- low-level helpers --------------------------------------- */

        function ws() {
            while (pos < n) {
                var c = json[pos];
                if (c !== ' ' && c !== '\t' && c !== '\r' && c !== '\n') { break; }
                pos++;
            }
        }

        function readString() {
            pos++;  /* skip opening " */
            var s = '';
            while (pos < n) {
                var c = json[pos++];
                if (c === '"') { break; }
                if (c === '\\') {
                    var e = json[pos++];
                    s += (e === 'n' ? '\n' : e === 't' ? '\t' :
                          e === 'r' ? '\r' : e);
                } else {
                    s += c;
                }
            }
            return s;
        }

        /* Fast-forward over a JSON value without capturing it.
         * Uses a depth counter to handle arbitrarily nested structures. */
        function skipValue() {
            ws();
            if (pos >= n) { return; }
            var c = json[pos];
            if (c === '"') {
                readString();
            } else if (c === '{' || c === '[') {
                pos++;
                var depth = 1;
                while (pos < n && depth > 0) {
                    var ch = json[pos++];
                    if (ch === '"') {
                        while (pos < n) {
                            var q = json[pos++];
                            if (q === '\\') { pos++; }
                            else if (q === '"') { break; }
                        }
                    } else if (ch === '{' || ch === '[') {
                        depth++;
                    } else if (ch === '}' || ch === ']') {
                        depth--;
                    }
                }
            } else {
                /* number / true / false / null */
                while (pos < n) {
                    var t = json[pos];
                    if (t === ',' || t === '}' || t === ']' ||
                        t === ' ' || t === '\t' || t === '\r' || t === '\n') { break; }
                    pos++;
                }
            }
        }

        /* Capture a scalar as its JS value; capture objects/arrays as
         * their raw JSON substring (avoids a full recursive parse).    */
        function captureValue() {
            ws();
            if (pos >= n) { return undefined; }
            var c = json[pos];
            if (c === '"') { return readString(); }
            if (c === '{' || c === '[') {
                var start = pos;
                skipValue();
                return json.slice(start, pos);
            }
            var s = pos;
            while (pos < n) {
                var t = json[pos];
                if (t === ',' || t === '}' || t === ']' ||
                    t === ' ' || t === '\t' || t === '\r' || t === '\n') { break; }
                pos++;
            }
            var raw = json.slice(s, pos);
            if (raw === 'true')  { return true;  }
            if (raw === 'false') { return false; }
            if (raw === 'null')  { return null;  }
            return +raw;
        }

        /* Walk a JSON object, pruning keys that cannot lead to any
         * required path.  Returns as soon as remain reaches zero.      */
        function walkObject() {
            pos++;  /* skip { */
            ws();
            if (pos < n && json[pos] === '}') { pos++; return; }

            while (pos < n) {
                ws();
                if (pos >= n || json[pos] !== '"') { break; }  /* malformed */
                var key = readString();
                ws();
                if (pos < n && json[pos] === ':') { pos++; }

                var cur = stack.length > 0
                    ? stack.join('.') + '.' + key
                    : key;

                if (needed[cur] && remain > 0) {
                    /* This path is required — capture the value.       */
                    result[cur] = captureValue();
                    remain--;
                    if (remain === 0) { return; }  /* ← early exit */

                } else if (ancestor[cur]) {
                    /* This key is an ancestor of a required path —
                     * recurse only if the value is an object.          */
                    ws();
                    if (pos < n && json[pos] === '{') {
                        stack.push(key);
                        walkObject();
                        stack.pop();
                    } else {
                        skipValue();  /* value is not an object; prune */
                    }

                } else {
                    skipValue();  /* key cannot lead to any required path */
                }

                if (remain === 0) { return; }

                ws();
                if (pos < n && json[pos] === '}') { pos++; return; }
                if (pos < n && json[pos] === ',') { pos++; }
            }
        }

        /* ---- entry point --------------------------------------------- */
        ws();
        if (pos < n && json[pos] === '{') { walkObject(); }
        return result;
    }

    /* ------------------------------------------------------------------
     * Routing helpers
     * ------------------------------------------------------------------ */

    /*
     * Returns true when fields contains a value for every path in
     * CFG.routingPaths.  Used to decide whether we have enough data to
     * commit to a routing target without scanning further.
     */
    function allPathsFound(fields) {
        for (var i = 0; i < CFG.routingPaths.length; i++) {
            if (!(CFG.routingPaths[i] in fields)) { return false; }
        }
        return true;
    }

    function pickBackend(fields) {
        for (var i = 0; i < CFG.routes.length; i++) {
            var route = CFG.routes[i];
            var match = true;
            var mkeys = Object.keys(route.match);
            for (var j = 0; j < mkeys.length; j++) {
                if (fields[mkeys[j]] !== route.match[mkeys[j]]) {
                    match = false;
                    break;
                }
            }
            if (match) { return route.backend; }
        }
        return CFG.defaultBackend;
    }

    /* ------------------------------------------------------------------
     * Locate the router server and its /route/ location
     * ------------------------------------------------------------------ */

    var srv;
    var servers = nginx.http.servers;
    for (var i = 0; i < servers.length; i++) {
        if (servers[i].name === 'router') { srv = servers[i]; break; }
    }
    if (!srv) { srv = servers[0]; }

    if (!srv) {
        nginx.log(nginx.ERR, 'json-router: no HTTP server found');
        return;
    }

    var routeLoc;
    var locs = srv.locations;
    for (var j = 0; j < locs.length; j++) {
        if (locs[j].path === '/route/') { routeLoc = locs[j]; break; }
    }

    if (!routeLoc) {
        nginx.log(nginx.ERR, 'json-router: /route/ location not found');
        return;
    }

    /* ------------------------------------------------------------------
     * Content handler
     * ------------------------------------------------------------------ */

    routeLoc.handler = async function (req) {

        /* ----------------------------------------------------------------
         * Phase 1 — synchronous scan of req.bodyPreread.
         *
         * bodyPreread returns the bytes already in nginx's connection read
         * buffer beyond the request headers (the same recv() that delivered
         * the request line and headers).  For JSON routing envelopes that
         * fit in one TCP segment this is the complete body — the routing
         * decision is made here with zero I/O and zero async overhead.
         * collectJsonPaths exits internally as soon as every path is found.
         * ---------------------------------------------------------------- */
        var preread  = req.bodyPreread;
        var fields   = Object.create(null);
        var scanDone = false;

        var preTrim = preread.trim();
        if (preTrim.length > 0 && preTrim[0] === '{') {
            fields   = collectJsonPaths(preTrim, CFG.routingPaths);
            scanDone = allPathsFound(fields);
            if (scanDone) {
                nginx.log(nginx.DEBUG,
                          'json-router: routing fields found in preread (' +
                          preread.length + ' B)');
            }
        }

        /* ----------------------------------------------------------------
         * Phase 2 — collect full body via req.bodyChunks().
         *
         * We need the complete body to forward it to the backend via
         * X-Forwarded-Body.  Iterating chunk-by-chunk also gives us the
         * opportunity to find routing fields incrementally: once scanDone
         * is set, subsequent chunks are only concatenated (no JSON parse).
         *
         * Note: nginx buffers the full body before the first chunk is
         * delivered, so no network-level memory is saved by breaking early.
         * The gain is CPU: collectJsonPaths scans are skipped once the
         * routing decision is committed.
         * ---------------------------------------------------------------- */
        var body = '';
        try {
            for await (var chunk of req.bodyChunks()) {
                body += chunk;
                if (!scanDone) {
                    var sofar = body.trim();
                    if (sofar.length > 0 && sofar[0] === '{') {
                        var f = collectJsonPaths(sofar, CFG.routingPaths);
                        fields = f;  /* always use latest scan */
                        if (allPathsFound(f)) {
                            scanDone = true;
                            nginx.log(nginx.DEBUG,
                                      'json-router: routing fields found after ' +
                                      body.length + ' B of body');
                            /* Continue iterating — need remaining chunks for
                             * X-Forwarded-Body; no more JSON parsing needed. */
                        }
                    }
                }
            }
        } catch (e) {
            req.respond(400,
                { 'Content-Type': 'application/json' },
                '{"error":"could not read body"}\n');
            return;
        }

        /* ----------------------------------------------------------------
         * Validation
         * ---------------------------------------------------------------- */
        var trimmed = body.trim();
        if (trimmed.length === 0) {
            req.respond(400,
                { 'Content-Type': 'application/json' },
                '{"error":"empty body"}\n');
            return;
        }
        if (trimmed[0] !== '{') {
            req.respond(400,
                { 'Content-Type': 'application/json' },
                '{"error":"body must be a JSON object"}\n');
            return;
        }

        /* pickBackend tolerates missing fields (uses '' as default).   */
        var backend = pickBackend(fields);

        if (body.length > CFG.bodyWarnSize) {
            nginx.log(nginx.WARN,
                      'json-router: body size ' + body.length +
                      ' B exceeds bodyWarnSize ' + CFG.bodyWarnSize +
                      ' B — X-Forwarded-Body may be truncated');
        }

        nginx.log(nginx.INFO,
                  'json-router: service=' + (fields['service']       || '-') +
                  ' region='              + (fields['tenant.region'] || '-') +
                  ' → ' + backend);

        /* ----------------------------------------------------------------
         * Forward to the chosen internal relay location via subrequest.
         *
         * The original body is passed in X-Forwarded-Body so the backend
         * receives the complete request payload.
         *
         * BODY FORWARDING LIMITATION
         * --------------------------
         * nginx subrequests do not carry a request body — they inherit
         * the parent request's method, URI, args, and headers only.
         * Passing the body in a header works for bodies within nginx's
         * large_client_header_buffers limit (default 8 KB × 4 buffers).
         *
         * For large-body routing the natural extension is:
         *
         *   req.setVariable('upstream_target', backendHost);
         *
         * paired with  proxy_pass http://$upstream_target/;  in nginx.conf.
         * That lets the normal proxy_pass path handle body streaming and
         * requires a req.setVariable() addition to the JS-Pilgrim API.  */
        var sub;
        try {
            sub = await req.subrequest(backend, {
                method:  req.method,
                args:    req.args,
                headers: {
                    'X-Forwarded-Body': body,
                    'X-Original-URI':   req.uri,
                    'Content-Type':
                        req.headers['content-type'] || 'application/json',
                },
            });
        } catch (e) {
            nginx.log(nginx.ERR,
                      'json-router: subrequest to ' + backend +
                      ' failed: ' + e);
            req.respond(502,
                { 'Content-Type': 'application/json' },
                '{"error":"backend unreachable"}\n');
            return;
        }

        req.respond(sub.status, sub.headers, sub.body);
    };

    nginx.log(nginx.NOTICE, 'json-router: initialised');

})();
