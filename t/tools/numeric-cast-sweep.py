#!/usr/bin/env python3
"""Audit src/js for numbers that are CAST out of JS rather than CHECKED.

JS_ToInt32()/JS_ToInt64() answer 0 for NaN, for {} and for "abc" without
reporting an error, and they hand back negatives that the caller then stores in
an unsigned field.  That shape has produced four real defects:

  peers[0].weight = -1     stored ~1.8e19 into the load balancer   (5186565a1)
  ssl.verifyDepth = {}     set certificate verification depth to 0 (0ebac7e47)
  respond(-1)              put "HTTP/1.1 18446744073709551615" on the wire
  stream peers             the same as the first, in the twin nobody ran

Every one was found separately, so this script exists to stop finding them one
at a time: it enumerates the shape mechanically, which is the only kind of
inventory that does not go stale as code is added.

Run it from the repo root:

    python3 t/tools/numeric-cast-sweep.py

It prints UNGUARDED sites to look at.  It is a lint, not an oracle: expect
false positives (a length read off an internal array is not caller input) and
judge each one.  New COM setters should use ngx_js_com_num_range() from
ngx_js_com.c instead of a bare conversion plus a cast.
"""

import glob
import io
import os
import re
import sys

CONV = re.compile(r'JS_To(Int32|Int64|Uint32|Float64|Index)\s*\(')

# the destination that makes an unchecked conversion dangerous
CAST = re.compile(
    r'=\s*\((ngx_uint_t|ngx_msec_t|time_t|size_t|uint32_t|uint64_t|ngx_flag_t)\)'
    r'\s*([A-Za-z_][A-Za-z0-9_]*)')

# anything between the conversion and the cast that constrains the value.
# NOTE: `>=` and `<=` must be matched before the bare `<`/`>` forms, and the
# first version of this script omitted them -- which reported two already-
# correct `ft >= 0` sites as unguarded.  A lint that cries wolf gets ignored.
GUARD = re.compile(
    r'ThrowRangeError|_num_range\s*\(|_num\s*\(|isnan|isinf'
    r'|[<>]=?\s*-?\d|[<>]=?\s*[A-Z_]{3,}')


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else 'src/js'
    files = sorted(glob.glob(os.path.join(root, '*.c')))
    if not files:
        print('no sources under %s' % root)
        return 2

    total = 0
    unguarded = []

    for path in files:
        lines = io.open(path, encoding='utf-8', errors='replace').read().split('\n')
        name = os.path.basename(path)
        for i, line in enumerate(lines):
            m = CAST.search(line)
            if not m:
                continue
            var = m.group(2)
            conv_at = None
            for j in range(max(0, i - 6), i):
                if CONV.search(lines[j]) and re.search(r'&\s*%s\b' % re.escape(var),
                                                       lines[j]):
                    conv_at = j
                    break
            if conv_at is None:
                continue
            total += 1
            window = '\n'.join(lines[conv_at:i + 1])
            if not GUARD.search(window):
                unguarded.append((name, i + 1, m.group(1), line.strip()[:66]))

    print('converted-then-cast sites: %d   unguarded: %d'
          % (total, len(unguarded)))
    if not unguarded:
        print('nothing to look at.')
        return 0

    print('-' * 78)
    cur = None
    for name, ln, dst, txt in unguarded:
        if name != cur:
            print('--- %s' % name)
            cur = name
        print('  %5d  -> %-11s %s' % (ln, dst, txt))
    print('-' * 78)
    print('Judge each one: a value read from an internal structure is not')
    print('caller input.  Real ones want ngx_js_com_num_range().')
    return 0


if __name__ == '__main__':
    sys.exit(main())
