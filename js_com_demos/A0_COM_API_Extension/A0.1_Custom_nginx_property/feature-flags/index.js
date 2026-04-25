// feature-flags — a minimal nginx COM extension.
//
// nginx.use() evaluates this module once in master (init-conf, before fork).
// The module installs nginx.featureFlags — a new first-class property on the
// nginx object — and every worker inherits it via COW fork.  Callers treat it
// exactly like any built-in nginx property.
//
// nginx.pluginConfig (second arg to nginx.use()):
//   { flags: ["name", ...] }   — list of flag names to manage
//
// nginx.featureFlags API:
//   get(name)           — returns true|false
//   set(name, enabled)  — writes to nginx.shared; instantly cross-worker
//   list()              — returns { name: true|false, ... }

(function () {

var cfg    = nginx.pluginConfig || {};
var _flags = cfg.flags || [];

/* Seed defaults in every worker before the first request arrives. */
nginx.broadcast(function () {
    _flags.forEach(function (name) {
        if (nginx.shared.get('flag.' + name) === undefined) {
            nginx.shared.set('flag.' + name, '0');
        }
    });
});

/* Extend the nginx COM object with a new property. */
nginx.featureFlags = {
    get: function (name) {
        return nginx.shared.get('flag.' + name) === '1';
    },
    set: function (name, enabled) {
        nginx.shared.set('flag.' + name, enabled ? '1' : '0');
    },
    list: function () {
        var out = {};
        _flags.forEach(function (name) {
            out[name] = nginx.shared.get('flag.' + name) === '1';
        });
        return out;
    }
};

})();
