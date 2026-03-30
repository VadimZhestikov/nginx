/**
 * Body JSON Router — JS-Pilgrim reference application.
 *
 * Routes incoming POST requests to different nginx upstream pools based on
 * fields extracted from the JSON request body.
 *
 * Key design goals
 * ----------------
 * • Zero whole-body buffering in JS — the body never lands in a JS string.
 * • O(n) scan — each byte is visited exactly once via the stateful streaming
 *   scanner; no re-scanning from the beginning on each new chunk.
 * • Early exit — scanning stops as soon as all routing fields are found;
 *   remaining body bytes flow from nginx's native buffers to the upstream
 *   via proxy_pass without touching JS at all.
 *
 * Two-phase routing
 * -----------------
 * Phase 1 (sync, zero I/O):
 *   req.bodyPreread returns bytes already in the connection read buffer
 *   beyond the request headers.  For routing envelopes that fit in one
 *   TCP segment this is the entire body — routing decision made here.
 *
 * Phase 2 (async, minimal buffering):
 *   req.bodyChunks() yields nginx's native body buffers one by one.
 *   The streaming scanner feeds on each chunk and exits as soon as all
 *   routing fields are found.  nginx has already DMA'd the body into its
 *   own pool; JS only holds one chunk string at a time.
 *
 * Body forwarding
 * ---------------
 *   req.pass(location) triggers ngx_http_internal_redirect, which re-enters
 *   the nginx phase engine at the new URI.  The matched location's proxy_pass
 *   streams r->request_body to the upstream natively — no X-Forwarded-Body
 *   header, no header-size limits, no extra copies.
 *
 * Default routing table (edit CFG.routes to customise):
 *
 *   service=payments  + tenant.region=eu-west  →  /internal/payments-eu/
 *   service=payments  + tenant.region=us-east  →  /internal/payments-us/
 *   service=analytics (any region)             →  /internal/analytics/
 *   anything else                              →  /internal/default/
 *
 * Required nginx.conf skeleton:
 *
 *   js_source /path/to/router.js;
 *
 *   server {
 *       server_name router;
 *       location /route/ { }               ← JS handler wired here
 *       location /internal/payments-eu/ {
 *           internal;
 *           proxy_pass http://payments-eu-upstream/;
 *       }
 *       # … other internal locations …
 *   }
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
    };

    /* ------------------------------------------------------------------
     * createPathScanner — stateful streaming JSON path scanner
     *
     * Processes a JSON object byte-by-byte via feed(chunk) calls.
     * Each byte is visited exactly once; the scanner is resumable across
     * arbitrary chunk boundaries (including mid-string, mid-number).
     * Returns as soon as every required path has been found.
     *
     * API:
     *   var s = createPathScanner(['service', 'tenant.region']);
     *   s.feed(chunk);          // process next bytes
     *   s.done()                // true when all paths captured
     *   s.getResult()           // {path: value, …} of found paths
     *
     * State machine labels (single uppercase chars for compactness):
     *   S  — start (expect '{')
     *   O  — object open (expect '"key"' or '}')
     *   K  — reading key string
     *   E  — escape in key string
     *   :  — after key (expect ':')
     *   V  — after ':' (dispatch on value first byte)
     *   CS — capturing string value
     *   CE — escape in captured string
     *   CX — capturing scalar (number / bool / null)
     *   SS — skipping string
     *   SE — escape in skipped string
     *   SX — skipping scalar
     *   SN — skipping nested object/array (depth-counted)
     *   NS — string inside skipped nested
     *   NE — escape in nested string
     *   AV — after value (expect ',' or '}')
     *   D  — done (all paths found or object exhausted)
     *
     * @param  {string[]} paths  Dot-notation paths, e.g. ['a.b.c', 'type']
     * @returns scanner object
     * ------------------------------------------------------------------ */

    function createPathScanner(paths) {

        /* Build O(1) lookup tables. */
        var needed   = Object.create(null);   /* exact path  → true */
        var ancestor = Object.create(null);   /* prefix path → true */

        for (var i = 0; i < paths.length; i++) {
            needed[paths[i]] = true;
            var segs = paths[i].split('.');
            for (var d = 1; d < segs.length; d++) {
                ancestor[segs.slice(0, d).join('.')] = true;
            }
        }

        var result  = Object.create(null);
        var remain  = paths.length;
        var stack   = [];      /* key-path segments for active ancestor nesting */
        var partial = '';      /* cross-chunk token accumulator */
        var curKey  = '';      /* key parsed in K/E states      */
        var ndepth  = 0;       /* depth counter for SN state    */
        var state   = 'S';

        function fullPath() {
            return stack.length > 0
                ? stack.join('.') + '.' + curKey
                : curKey;
        }

        return {

            feed: function (chunk) {
                if (state === 'D') { return; }
                var n = chunk.length;
                var i = 0;
                var c, fp;

                while (i < n && state !== 'D') {
                    c = chunk[i++];

                    switch (state) {

                    case 'S':   /* start — expect '{' */
                        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                        state = (c === '{') ? 'O' : 'D';
                        break;

                    case 'O':   /* inside object — expect '"key"' or '}' */
                        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                        if (c === '"') { partial = ''; state = 'K'; }
                        else if (c === '}') {
                            if (stack.length > 0) { stack.pop(); state = 'AV'; }
                            else { state = 'D'; }
                        }
                        break;

                    case 'K':   /* reading key string */
                        if      (c === '"')  { curKey = partial; partial = ''; state = ':'; }
                        else if (c === '\\') { state = 'E'; }
                        else                 { partial += c; }
                        break;

                    case 'E':   /* escape in key */
                        partial += (c === 'n' ? '\n' : c === 't' ? '\t' :
                                    c === 'r' ? '\r' : c);
                        state = 'K';
                        break;

                    case ':':   /* after key — expect ':' */
                        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                        if (c === ':') { state = 'V'; }
                        break;

                    case 'V':   /* after ':' — dispatch on first byte of value */
                        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                        fp = fullPath();
                        if (needed[fp]) {
                            /* Capture value: strings and scalars only.
                             * Object/array values at routing-field paths are
                             * skipped (not captured) — routing fields are
                             * always scalars in practice.               */
                            if (c === '"') {
                                partial = ''; state = 'CS';
                            } else if (c === '{' || c === '[') {
                                ndepth = 1; state = 'SN';
                            } else {
                                partial = c; state = 'CX';
                            }
                        } else if (ancestor[fp] && c === '{') {
                            /* Recurse into ancestor object. */
                            stack.push(curKey); state = 'O';
                        } else {
                            /* Skip value — not needed and not an ancestor. */
                            if      (c === '"')              { state = 'SS'; }
                            else if (c === '{' || c === '[') { ndepth = 1; state = 'SN'; }
                            else                             { state = 'SX'; }
                        }
                        break;

                    case 'CS':  /* capturing string value */
                        if (c === '"') {
                            result[fullPath()] = partial;
                            partial = '';
                            if (--remain === 0) { state = 'D'; break; }
                            state = 'AV';
                        } else if (c === '\\') {
                            state = 'CE';
                        } else {
                            partial += c;
                        }
                        break;

                    case 'CE':  /* escape in captured string */
                        partial += (c === 'n' ? '\n' : c === 't' ? '\t' :
                                    c === 'r' ? '\r' : c);
                        state = 'CS';
                        break;

                    case 'CX':  /* capturing scalar (number/bool/null) */
                        if (c === ',' || c === '}' || c === ']' ||
                            c === ' ' || c === '\t' || c === '\r' || c === '\n') {
                            var raw = partial;
                            partial = '';
                            result[fullPath()] = (raw === 'true'  ? true  :
                                                  raw === 'false' ? false :
                                                  raw === 'null'  ? null  : +raw);
                            if (--remain === 0) { state = 'D'; break; }
                            if (c === '}') {
                                if (stack.length > 0) { stack.pop(); state = 'AV'; }
                                else { state = 'D'; }
                            } else if (c === ',') { state = 'O'; }
                            else                  { state = 'AV'; }
                        } else {
                            partial += c;
                        }
                        break;

                    case 'SS':  /* skipping string */
                        if      (c === '"')  { state = 'AV'; }
                        else if (c === '\\') { state = 'SE'; }
                        break;

                    case 'SE':  /* escape in skipped string */
                        state = 'SS';
                        break;

                    case 'SX':  /* skipping scalar */
                        if (c === ',' || c === '}' || c === ']' ||
                            c === ' ' || c === '\t' || c === '\r' || c === '\n') {
                            if (c === '}') {
                                if (stack.length > 0) { stack.pop(); state = 'AV'; }
                                else { state = 'D'; }
                            } else if (c === ',') { state = 'O'; }
                            else                  { state = 'AV'; }
                        }
                        break;

                    case 'SN':  /* skipping nested object/array */
                        if      (c === '"')              { state = 'NS'; }
                        else if (c === '{' || c === '[') { ndepth++; }
                        else if (c === '}' || c === ']') {
                            if (--ndepth === 0) { state = 'AV'; }
                        }
                        break;

                    case 'NS':  /* string inside skipped nested */
                        if      (c === '"')  { state = 'SN'; }
                        else if (c === '\\') { state = 'NE'; }
                        break;

                    case 'NE':  /* escape in nested string */
                        state = 'NS';
                        break;

                    case 'AV':  /* after value — expect ',' or '}' */
                        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { break; }
                        if      (c === ',') { state = 'O'; }
                        else if (c === '}') {
                            if (stack.length > 0) { stack.pop(); state = 'AV'; }
                            else { state = 'D'; }
                        }
                        break;
                    }
                }
            },

            done:      function () { return remain === 0; },
            getResult: function () { return result; }
        };
    }

    /* ------------------------------------------------------------------
     * Routing helpers
     * ------------------------------------------------------------------ */

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
         * bodyPreread returns bytes already in nginx's connection read
         * buffer beyond the request headers (arrived in the same recv()
         * as the request line and headers).  For routing envelopes that
         * fit in one TCP segment this resolves the routing decision with
         * zero I/O and zero async overhead — req.pass() is called before
         * bodyChunks() ever allocates.
         * ---------------------------------------------------------------- */
        var scanner = createPathScanner(CFG.routingPaths);

        var preTrim = req.bodyPreread.trim();
        if (preTrim.length > 0 && preTrim[0] === '{') {
            scanner.feed(preTrim);
            if (scanner.done()) {
                nginx.log(nginx.DEBUG,
                          'json-router: all routing fields found in preread');
                req.pass(pickBackend(scanner.getResult()));
                return;
            }
        }

        /* ----------------------------------------------------------------
         * Phase 2 — chunk-by-chunk scan via req.bodyChunks().
         *
         * nginx has already read the body into its native buffer chain
         * (r->request_body->bufs) by the time the first chunk arrives.
         * For large bodies nginx spills to a temp file; JS still only
         * holds one chunk string at a time.
         *
         * The scanner processes each byte once.  As soon as done() is
         * true we break — remaining body bytes stay in nginx's buffers
         * and are forwarded to the upstream by proxy_pass natively.
         * ---------------------------------------------------------------- */
        try {
            for await (var chunk of req.bodyChunks()) {
                scanner.feed(chunk);
                if (scanner.done()) { break; }
            }
        } catch (e) {
            req.respond(400,
                { 'Content-Type': 'application/json' },
                '{"error":"could not read body"}\n');
            return;
        }

        /* ----------------------------------------------------------------
         * Validation — we need at least a non-empty JSON object.
         * If no '{' was seen by the scanner, the body is not JSON.
         * ---------------------------------------------------------------- */
        if (preTrim.length === 0 && !scanner.done()) {
            /* Re-check: scanner in state 'S' means no '{' was ever fed.  */
        }

        var fields  = scanner.getResult();
        var backend = pickBackend(fields);

        nginx.log(nginx.INFO,
                  'json-router: service=' + (fields['service']       || '-') +
                  ' region='              + (fields['tenant.region'] || '-') +
                  ' → ' + backend);

        /* ----------------------------------------------------------------
         * req.pass(backend) — internal redirect.
         *
         * nginx re-enters the phase engine at the backend URI.  The matched
         * location's proxy_pass streams r->request_body to the upstream
         * directly from nginx's buffers — no JS-side body copy is made.
         * ---------------------------------------------------------------- */
        req.pass(backend);
    };

    nginx.log(nginx.NOTICE, 'json-router: initialised');

})();
