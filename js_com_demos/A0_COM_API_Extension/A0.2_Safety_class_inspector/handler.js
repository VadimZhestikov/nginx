// A0.2 — COM Safety-Class Inspector
//
// Walks the live nginx COM tree via nginx.describe() and renders a
// traffic-light report of every settable property/method, classified by:
//
//   class         safe (green) | guarded (amber) | irreversible (red)
//   propagation   worker-local | zoned-shared | auto-shared
//   requestScoped honours setWriteMode('local')
//
// This is the seed of the Layer 3 admin console: it surfaces, for an
// operator, exactly which runtime changes are safe, which need cross-worker
// fan-out, and which can never be undone — without reading any C or JS source.
//
// The class set is no longer hard-coded: nginx.describe() with no path is the
// discovery root — it returns every classifiable COM class — and describe(path)
// now lists read-only getters alongside settable members, so the report is a
// complete per-object reference.
//
// Endpoints (installed per worker via nginx.broadcast):
//   GET /            HTML traffic-light table + class-catalog panel
//   GET /catalog     JSON discovery root: [{class, members:[…]}] for all classes
//   GET /inspect     full JSON report (array of {path, members:[…]})
//   GET /inspect/sum JSON summary counts by class + propagation

nginx.broadcast(function () {

    /* Sub-objects to probe on each location.  describe() returns [] for any
     * that the location doesn't expose, so unconfigured ones are skipped. */
    var LOC_SUBOBJECTS = [
        'proxy', 'gzip', 'headers', 'rewrite', 'access', 'auth',
        'limitReq', 'limitConn', 'fastcgi', 'log', 'realip', 'charset',
        'autoindex', 'referer', 'dav', 'ssi', 'userid', 'addition',
        'gunzip', 'slice', 'imageFilter', 'xslt', 'secureLink', 'mp4',
        'randomIndex', 'authRequest', 'gzipStatic', 'memcached',
        'scgi', 'uwsgi', 'mirror'
    ];

    /* Build [{path, members}] for one COM object path, skipping empties. */
    function describeNode(path) {
        var members = nginx.describe(path);
        if (!members || !members.length) { return null; }
        return { path: path, members: members };
    }

    /* The discovery root: nginx.describe() with no path lists every
     * classifiable COM class and its members.  The inspector no longer has to
     * know the class set a priori — it can show "what exists" straight from
     * the engine. */
    function catalog() {
        return nginx.describe();   /* [{ class, members:[…names] }] */
    }

    /* Walk server[0]: its locations + sub-objects, plus all upstream peers. */
    function walk() {
        var nodes = [];
        var srv   = nginx.http.servers[0];

        /* nginx.http itself — its topology methods (addServer/attach are
         * irreversible, removeServer/removeListener guarded) are now
         * classified, so the report shows a real 🔴 tier. */
        nodes.push(describeNode('http'));
        nodes.push(describeNode('cycle'));
        nodes.push(describeNode('http.servers[0]'));

        srv.locations.forEach(function (loc, li) {
            var base = 'http.servers[0].locations[' + li + ']';
            nodes.push(describeNode(base));
            LOC_SUBOBJECTS.forEach(function (sub) {
                if (loc[sub]) {
                    nodes.push(describeNode(base + '.' + sub));
                }
            });
        });

        nginx.http.upstreams.forEach(function (up, ui) {
            up.peers.forEach(function (peer, pi) {
                nodes.push(describeNode(
                    'http.upstreams[' + ui + '].peers[' + pi + ']'));
            });
        });

        nodes.push(describeNode('events'));

        return nodes.filter(function (n) { return n !== null; });
    }

    function summarise(nodes) {
        var cls  = { safe: 0, guarded: 0, irreversible: 0, readonly: 0 };
        var prop = { 'worker-local': 0, 'zoned-shared': 0, 'auto-shared': 0 };
        var reqScoped = 0, total = 0;
        nodes.forEach(function (n) {
            n.members.forEach(function (m) {
                total++;
                if (cls[m.class] !== undefined)  { cls[m.class]++; }
                if (prop[m.propagation] !== undefined) { prop[m.propagation]++; }
                if (m.requestScoped) { reqScoped++; }
            });
        });
        return { total: total, byClass: cls, byPropagation: prop,
                 requestScoped: reqScoped };
    }

    /* ---- HTML rendering -------------------------------------------------- */

    var CLASS_COLOR = {
        safe:         '#1a7f37',   /* green  */
        guarded:      '#9a6700',   /* amber  */
        irreversible: '#cf222e',   /* red    */
        readonly:     '#57606a'    /* grey   */
    };
    var CLASS_DOT = { safe: '🟢', guarded: '🟡', irreversible: '🔴',
                      readonly: '⚪' };

    function esc(s) {
        return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
                        .replace(/>/g, '&gt;');
    }

    function renderCatalog(cat) {
        var cells = cat.map(function (e) {
            return '<span class="cls">' + esc(e.class)
                 + ' <small>(' + e.members.length + ')</small></span>';
        }).join('');
        return '<details class="cat"><summary><b>' + cat.length
             + ' COM classes</b> exposed by <code>nginx.describe()</code> '
             + '(the discovery root) — click to expand</summary>'
             + '<div class="grid">' + cells + '</div></details>';
    }

    function renderHtml(nodes, sum, cat) {
        var rows = '';
        nodes.forEach(function (n) {
            rows += '<tr class="grp"><td colspan="6">' + esc(n.path)
                  + '</td></tr>';
            n.members.forEach(function (m) {
                var color = CLASS_COLOR[m.class] || '#57606a';
                rows += '<tr>'
                  + '<td class="mem">' + esc(m.name) + '</td>'
                  + '<td>' + esc(m.type) + '</td>'
                  + '<td style="color:' + color + ';font-weight:600">'
                      + (CLASS_DOT[m.class] || '') + ' ' + esc(m.class) + '</td>'
                  + '<td>' + esc(m.propagation) + '</td>'
                  + '<td style="text-align:center">'
                      + (m.requestScoped ? '✓' : '·') + '</td>'
                  + '<td class="note">' + (m.note ? esc(m.note) : '') + '</td>'
                  + '</tr>';
            });
        });

        return '<!doctype html><html><head><meta charset="utf-8">'
          + '<title>COM Safety-Class Inspector</title><style>'
          + 'body{font:14px/1.4 system-ui,sans-serif;margin:24px;color:#1f2328}'
          + 'h1{font-size:20px}'
          + '.sum{margin:12px 0 20px;padding:12px;background:#f6f8fa;'
              + 'border-radius:8px}'
          + '.sum b{margin-right:14px}'
          + 'table{border-collapse:collapse;width:100%}'
          + 'th,td{padding:5px 10px;border-bottom:1px solid #eaeef2;'
              + 'text-align:left;font-size:13px}'
          + 'th{background:#f6f8fa;position:sticky;top:0}'
          + '.grp td{background:#eef3f8;font-family:monospace;font-weight:600}'
          + '.mem{font-family:monospace}'
          + '.note{color:#57606a;font-size:12px}'
          + '.cat{margin:0 0 16px;padding:10px 12px;background:#f6f8fa;'
              + 'border-radius:8px}'
          + '.cat summary{cursor:pointer}'
          + '.cat .grid{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}'
          + '.cls{font-family:monospace;font-size:12px;background:#fff;'
              + 'border:1px solid #d0d7de;border-radius:6px;padding:2px 7px}'
          + '.cls small{color:#57606a}'
          + '</style></head><body>'
          + '<h1>nginx COM — Mutation Safety-Class Inspector</h1>'
          + renderCatalog(cat)
          + '<div class="sum">'
          + '<b>' + sum.total + ' members</b>'
          + '<b style="color:#1a7f37">🟢 ' + sum.byClass.safe + ' safe</b>'
          + '<b style="color:#9a6700">🟡 ' + sum.byClass.guarded + ' guarded</b>'
          + '<b style="color:#cf222e">🔴 ' + sum.byClass.irreversible
              + ' irreversible</b>'
          + '<b style="color:#57606a">⚪ ' + sum.byClass.readonly
              + ' read-only</b>'
          + '<br><b>propagation:</b>'
          + 'worker-local ' + sum.byPropagation['worker-local'] + ' · '
          + 'zoned-shared ' + sum.byPropagation['zoned-shared'] + ' · '
          + 'auto-shared ' + sum.byPropagation['auto-shared']
          + ' &nbsp; <b>request-scoped:</b> ' + sum.requestScoped
          + '</div>'
          + '<table><thead><tr><th>member</th><th>type</th><th>class</th>'
          + '<th>propagation</th><th>req-scoped</th><th>note</th></tr></thead>'
          + '<tbody>' + rows + '</tbody></table>'
          + '<p class="note">🟢 safe: reversible value change · '
          + '🟡 guarded: changes dispatch/live state, needs care + fan-out · '
          + '🔴 irreversible: cannot be undone for the process lifetime. '
          + 'zoned-shared peers are cross-worker only when the upstream has a '
          + 'zone; otherwise the write is worker-local and needs fan-out.</p>'
          + '</body></html>';
    }

    /* ---- Endpoint wiring ------------------------------------------------- */

    var srv  = nginx.http.servers[0];
    var locs = srv.locations;
    function at(path, fn) {
        var l = locs.find(function (l) { return l.path === path; });
        if (l) { l.handler = fn; }
    }

    at('/', function (r) {
        var nodes = walk();
        r.respond(200, {'Content-Type': 'text/html; charset=utf-8'},
            renderHtml(nodes, summarise(nodes), catalog()));
    });

    at('/catalog', function (r) {
        /* the discovery root straight from the engine */
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify(catalog(), null, 1) + '\n');
    });

    at('/inspect', function (r) {
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify(walk(), null, 1) + '\n');
    });

    at('/inspect/sum', function (r) {
        r.respond(200, {'Content-Type': 'application/json'},
            JSON.stringify(summarise(walk()), null, 1) + '\n');
    });
});
