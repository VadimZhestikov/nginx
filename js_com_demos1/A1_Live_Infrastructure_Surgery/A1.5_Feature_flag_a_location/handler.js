// A1.5 — Feature Flag a Location
//
// A simple boolean flag gates access to a /beta/ endpoint at runtime.
// Toggle is exposed via POST /admin/toggle/ — no config edit, no reload.
//
// Extended to show per-flag granularity: multiple flags in a flags object.

(function () {
    var locs = nginx.http.servers[0].locations;

    function findLoc(path) {
        return locs.find(function (l) { return l.path === path; });
    }

    // Feature flags registry
    var flags = {
        beta:       false,   // /beta/ feature
        darkMode:   false,   // hypothetical dark-mode toggle
        newCheckout: false,  // hypothetical checkout redesign
    };

    // /stable/ — always on; returns current flag state so tests can verify
    findLoc('/stable/').handler = function (r) {
        r.respond(200, {}, 'stable: always available\n');
    };

    // /beta/ — gated by flags.beta
    findLoc('/beta/').handler = function (r) {
        if (!flags.beta) {
            r.respond(404, {}, 'beta feature is disabled\n');
            return;
        }
        r.respond(200, {}, 'beta feature is enabled\n');
    };

    // POST /admin/toggle/?<flagName> — flip the named flag
    findLoc('/admin/toggle/').handler = function (r) {
        if (r.method !== 'POST') { r.respond(405, {}, 'Method Not Allowed\n'); return; }

        var name = r.args || 'beta';
        if (!(name in flags)) {
            r.respond(404, {}, 'unknown flag: ' + name + '\n');
            return;
        }

        flags[name] = !flags[name];
        nginx.log(4, 'Feature flag toggled: ' + name + ' = ' + flags[name]);
        r.respond(200, {}, name + '=' + flags[name] + '\n');
    };

    // GET /admin/flags/ — list all flags and their state
    findLoc('/admin/flags/').handler = function (r) {
        var lines = Object.keys(flags).map(function (k) {
            return k + '=' + flags[k];
        });
        r.respond(200, {}, lines.join('\n') + '\n');
    };
})();
