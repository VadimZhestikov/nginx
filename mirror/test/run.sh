#!/usr/bin/env bash
# Acceptance test for the mirror ev.* runtime surface (mirror/lib/mirror.js).
# Runs standalone under qjs (the QuickJS REPL) — no nginx build or running
# instance required. Mirrors mirror/transpile/run.sh.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"      # mirror/test
ROOT="$(cd "$DIR/../.." && pwd)"          # nginx repo root
QJS="${QJS:-$ROOT/../quickjs/qjs}"        # sibling quickjs checkout

if [ ! -x "$QJS" ]; then
    echo "qjs not found at $QJS — build it with: make -C ../quickjs qjs" >&2
    echo "(or set QJS=/path/to/qjs)" >&2
    exit 2
fi

# ev.test.js is a module: it std.loadFile()s mirror.js from the path passed in
# scriptArgs, so there is a single canonical copy.
"$QJS" "$DIR/ev.test.js" "$DIR/../lib/mirror.js"
