#!/usr/bin/env python3
"""Audit what src/js does with the value a JS callback RETURNS.

The companion to numeric-cast-sweep.py, and it exists because that one has a
blind spot: it looks for a converted value cast into an unsigned field, which is
the shape of an ARGUMENT coming in through argv[].  A callback return is a
different shape, and the worst bug of that family got through:

    upstream.onSelectPeer(function (peers) { ... })   // no return on one path

`JS_ToInt32()` answers 0 for undefined without an error, so a selection function
that fell off its end sent EVERY request to peer 0 (a321849fa).  Running the
numeric lint over that code reports it clean.

A callback return deserves more suspicion than an argument, not less: it is
produced per-request by a script that can be silently wrong, and the value
steers behaviour — which peer, which status, whether to reject a handshake.

Run from the repo root:

    python3 t/tools/callback-return-sweep.py

It reports the sites that CONSUME a return value.  It is a lint, not an oracle:
it cannot tell a correct consumer from an incorrect one.  Read each and ask the
only question that matters:

    when the callback returns the wrong kind of thing, what happens?

The answer should be a type guard and a documented default, never a silent
coercion.  The codebase has four good examples to copy:

  ngx_js_com_ssl.c   `JS_IsBool(ret) && !JS_ToBool(...)` — only an explicit
                     `return false` rejects a handshake; anything else fails
                     OPEN, which is right for a hook that should not be able to
                     kill connections by being buggy.
  ngx_js_module.c    `JS_IsFunction(sctx, fn)` or throw — fail CLOSED, which is
                     right for admission.
  ngx_js_http_module.c
                     `JS_IsObject(ret) ? JS_GetPropertyStr(...) : JS_UNDEFINED`
                     — type-guard before reading a property.
  ngx_js_com_upstream.c
                     the fixed one: a real, finite, in-range number or the
                     documented -1 fallback.

Last full read: 2026-09-11 — 65 call sites, 11 consuming the return, and all 11
correct (the LB one having been fixed).  A clean result, recorded so the next
person does not have to re-derive it.
"""

import glob
import io
import os
import re
import sys

CALL = re.compile(r'(\w+)\s*=\s*JS_Call\s*\(')

# "consumed" = anything reads the value: a conversion to C, a property read, a
# type test.  An earlier version looked only for JS_To* and reported 2 consumers
# instead of 11, missing every `gen = JS_Call(...)` that then reads .next --
# a lint that under-reports is how the thing it hunts stays hidden.
CONSUME = re.compile(
    r'(?:JS_To(?:Int32|Int64|Uint32|Float64|Bool|CString|CStringLen)'
    r'|JS_GetPropertyStr|JS_GetPropertyUint32'
    r'|JS_IsFunction|JS_IsObject|JS_IsString|JS_IsNumber|JS_IsBool)'
    r'\s*\([^;]*\b%s\b')

EXC = re.compile(r'JS_IsException\s*\(\s*%s\s*\)')
GUARD = re.compile(r'JS_Is(?:Bool|Number|String|Function|Object|Array)\s*\('
                   r'[^;]*\b%s\b')


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'src/js'
    files = sorted(glob.glob(os.path.join(root, '*.c')))
    if not files:
        print('no sources under %s' % root)
        return 2

    total = 0
    consumers = []

    for path in files:
        lines = io.open(path, encoding='utf-8',
                        errors='replace').read().split('\n')
        name = os.path.basename(path)
        for i, line in enumerate(lines):
            if 'JS_Call(' not in line:
                continue
            total += 1
            m = CALL.search(line)
            if not m:
                continue                      # result discarded inline
            var = m.group(1)
            window = '\n'.join(lines[i:i + 25])
            if not re.search(CONSUME.pattern % re.escape(var), window):
                continue
            consumers.append((
                name, i + 1, var,
                bool(re.search(EXC.pattern % re.escape(var), window)),
                bool(re.search(GUARD.pattern % re.escape(var), window)),
                line.strip()[:56]))

    print('JS_Call sites: %d   consuming the return: %d'
          % (total, len(consumers)))
    print('=' * 78)
    cur = None
    for name, ln, var, exc, guard, txt in consumers:
        if name != cur:
            print('--- %s' % name)
            cur = name
        flags = []
        if not exc:
            flags.append('NO-EXC-CHECK')
        if not guard:
            flags.append('NO-TYPE-GUARD')
        print('  %5d  %-9s %-26s %s' % (ln, var, ','.join(flags) or '-', txt))
    print('=' * 78)
    print('NO-TYPE-GUARD is the one to read first: it means the value is used')
    print('without anything having checked what it is.  That is how a callback')
    print('that returned nothing came to steer every request to one backend.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
