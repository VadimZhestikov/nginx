// mirror typed API schema — M2 deliverable.
//
// A machine-readable STATIC TYPE SIGNATURE for the mirror ev.* surface. Today
// the only machine-readable artifact for ev.* is CAPS (lib/mirror.js), which is
// a per-event allow-list of NAMES: no parameter types, no arity, no return
// types. That is enough to gate a call, but not enough to
//   * bind a typed AST against the API              (milestone M4), or
//   * lower it to unboxed C with no JSValue boxing  (milestone M5),
// both of which need every expression's type to be concrete.
//
// The vocabulary below is therefore chosen to be lowerable, not merely
// descriptive: each type names a concrete machine representation.
//
//   i64  f64  bool  void          scalars
//   str                           string; see `mem` for the ownership ABI
//   T?                            optional (JS undefined) — e.g. "str?"
//   record                        open JS object (unknown keys) -> boxed
//   array<T>                      homogeneous list
//   handle<Name>                  opaque COM/host handle, not introspectable
//   any                           ESCAPE HATCH: forces the boxed/interpreted
//                                 path (maxim phase-34 hybrid). Deliberately
//                                 ugly so `any` in a signature is visible as
//                                 "this member cannot be lowered".
//
// `mem` (strings only) records the ownership ABI that M5 needs to avoid
// refcount churn — the design note calls this out as a hard part:
//   'borrowed' — points into nginx-owned memory, valid for the current event
//                only; must be copied to outlive it.
//   'owned'    — freshly allocated, caller owns.
//
// SAFETY CLASS reuses the pilgrim describe() vocabulary verbatim
// (readonly|safe|guarded|irreversible) so the two registries stay speakable in
// one language when the C side is typed in M2b.
//
// VERSIONING: `schemaVersion` is load-bearing, not decorative. A tenant policy
// compiled against schema vN must refuse to run against vN+1 — the same
// stale-artifact failure mode as the JIT's __jit_cv_ codegen-version guard,
// where a cache compiled by an older codegen silently served wrong code. Bump
// it on ANY signature change.

(function () {
    'use strict';

    var SCHEMA_VERSION = 1;

    // ---- ev.table facade (iRules `table`) -----------------------------------
    // Reached as ev.table.<m>(); gated as a whole by the 'table' capability.
    var TABLE_MEMBERS = {
        get:    { kind: 'method', params: [{ name: 'key', type: 'str', mem: 'borrowed' }],
                  returns: 'any?', klass: 'safe',
                  note: 'JSON-decoded; any? because the stored value is untyped' },
        set:    { kind: 'method',
                  params: [{ name: 'key', type: 'str', mem: 'borrowed' },
                           { name: 'value', type: 'any' },
                           { name: 'ttlSeconds', type: 'i64', optional: true }],
                  returns: 'any', klass: 'safe',
                  note: 'returns the stored value; ttl 0/absent = never expires' },
        incr:   { kind: 'method',
                  params: [{ name: 'key', type: 'str', mem: 'borrowed' },
                           { name: 'delta', type: 'i64', optional: true, dflt: 1 }],
                  returns: 'i64', klass: 'safe',
                  note: 'atomic across workers on the shared backend' },
        'delete': { kind: 'method', params: [{ name: 'key', type: 'str', mem: 'borrowed' }],
                  returns: 'bool', klass: 'safe' },
        keys:   { kind: 'method', params: [], returns: 'array<str>', klass: 'safe',
                  mem: 'owned' },
        ttl:    { kind: 'method', params: [{ name: 'key', type: 'str', mem: 'borrowed' }],
                  returns: 'i64?', klass: 'safe',
                  note: 'null when absent, -1 when it never expires, else seconds' }
    };

    // ---- ev.* for onRequestHeaders (the M2 target capability set) ------------
    // `capability` is the CAPS name that gates the member: M3's compile-time
    // check reduces to "does this name resolve in the granted capability set".
    var ON_REQUEST_HEADERS = {
        clientAddr: { kind: 'getter', returns: 'str', mem: 'borrowed', klass: 'readonly',
                      capability: 'clientAddr',
                      note: 'r.variable("remote_addr") on the request path' },
        // Deliberately typed str, NOT i64: on the request path this comes from
        // r.variable("remote_port") (a string), while onClientAccept exposes
        // conn.remotePort (a number). The schema records what this event returns.
        clientPort: { kind: 'getter', returns: 'str', mem: 'borrowed', klass: 'readonly',
                      capability: 'clientPort',
                      note: 'string here (r.variable); numeric at onClientAccept' },
        method:     { kind: 'getter', returns: 'str', mem: 'borrowed', klass: 'readonly',
                      capability: 'method' },
        uri:        { kind: 'getter', returns: 'str', mem: 'borrowed', klass: 'readonly',
                      capability: 'uri' },
        // flow/ctx are open bags of caller-defined properties, so they are `any`
        // by construction: nothing can be statically known about their contents.
        // They are the visible boundary of what M5 can lower.
        flow:       { kind: 'getter', returns: 'any', klass: 'safe', capability: 'flow',
                      note: 'connection-scoped store (r.connCtx); survives keepalive' },
        ctx:        { kind: 'getter', returns: 'any', klass: 'safe', capability: 'ctx',
                      note: 'request-scoped store (r.ctx)' },
        table:      { kind: 'namespace', members: TABLE_MEMBERS, klass: 'safe',
                      capability: 'table', note: 'cross-worker store' },

        header:     { kind: 'method',
                      params: [{ name: 'name', type: 'str', mem: 'borrowed' }],
                      returns: 'str?', mem: 'borrowed', klass: 'readonly',
                      capability: 'header', note: 'case-insensitive' },
        cookie:     { kind: 'method',
                      params: [{ name: 'name', type: 'str', mem: 'borrowed' }],
                      returns: 'str?', mem: 'owned', klass: 'readonly',
                      capability: 'cookie', note: 'parsed out of the Cookie header' },

        respond:    { kind: 'method',
                      params: [{ name: 'code', type: 'i64' },
                               { name: 'headers', type: 'record', optional: true },
                               { name: 'body', type: 'str', mem: 'borrowed', optional: true }],
                      returns: 'void', klass: 'guarded',
                      capability: 'respond', effects: ['finalize.response'],
                      note: 'finalizes the request; cancels the hook chain in C' },
        redirect:   { kind: 'method',
                      params: [{ name: 'url', type: 'str', mem: 'borrowed' },
                               { name: 'code', type: 'i64', optional: true, dflt: 302 }],
                      returns: 'void', klass: 'guarded',
                      capability: 'redirect', effects: ['finalize.response'],
                      note: 'respond + Location header' },
        selectUpstream: { kind: 'method',
                      params: [{ name: 'name', type: 'str', mem: 'borrowed' }],
                      returns: 'void', klass: 'guarded',
                      capability: 'selectUpstream', effects: ['set.variable.mirror_upstream'],
                      note: 'resolved by proxy_pass http://$mirror_upstream' }
    };

    var SCHEMA = {
        schemaVersion: SCHEMA_VERSION,
        events: {
            onRequestHeaders: ON_REQUEST_HEADERS
        },

        // Convenience: the capability names this schema declares for an event.
        // The drift test asserts this equals CAPS[event] exactly.
        capabilitiesOf: function (event) {
            var m = SCHEMA.events[event];
            if (!m) { return []; }
            return Object.keys(m).map(function (k) {
                return m[k].capability || k;
            }).sort();
        },

        // Arity a conforming implementation must expose (fn.length): the count
        // of leading non-optional params. Used by the drift test to catch a
        // signature that has drifted from the implementation.
        requiredArity: function (sig) {
            if (sig.kind !== 'method') { return null; }
            var n = 0;
            for (var i = 0; i < sig.params.length; i++) {
                if (sig.params[i].optional) { break; }
                n++;
            }
            return n;
        }
    };

    if (typeof globalThis !== 'undefined') {
        if (globalThis.mirror) { globalThis.mirror.schema = SCHEMA; }
        globalThis.mirrorSchema = SCHEMA;
    }
})();
